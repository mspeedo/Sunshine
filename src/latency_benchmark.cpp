#include "latency_benchmark.h"

#ifdef _WIN32

#include "logging.h"
#include "platform/common.h"
#include "platform/windows/utf_utils.h"
#include "rtsp.h"
#include "stream.h"

#include <boost/filesystem.hpp>
#include <boost/process/v1.hpp>

#include <windows.h>
#include <sddl.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

using namespace std::literals;

namespace latency_benchmark {
  namespace {
    constexpr DWORD GRACEFUL_STOP_TIMEOUT_MS = 1000;
    constexpr DWORD FORCE_STOP_TIMEOUT_MS = 1000;
    constexpr uint32_t WAIT_MAP_MAGIC = 0x4D4C4257U;  // "MLBW"
    constexpr uint32_t WAIT_MAP_VERSION = 1;
    constexpr size_t WAIT_RECORD_COUNT = 64;

    struct wait_record_t {
      volatile LONG64 sequence;
      volatile LONG64 wait_us;
    };

    struct shared_wait_state_t {
      uint32_t magic;
      uint32_t version;
      wait_record_t records[WAIT_RECORD_COUNT];
    };

    struct state_t {
      std::mutex mutex;
      HANDLE process = nullptr;
      HANDLE stop_event = nullptr;
      HANDLE wait_mapping = nullptr;
      shared_wait_state_t *wait_state = nullptr;
      DWORD process_id = 0;
      uint64_t control_event_serial = 0;
    };

    state_t &state() {
      static auto *value = new state_t;
      return *value;
    }

    void clear_process_locked(state_t &value) {
      if (value.process) {
        CloseHandle(value.process);
      }
      if (value.stop_event) {
        CloseHandle(value.stop_event);
      }
      if (value.wait_state) {
        UnmapViewOfFile(value.wait_state);
      }
      if (value.wait_mapping) {
        CloseHandle(value.wait_mapping);
      }
      value.process = nullptr;
      value.stop_event = nullptr;
      value.wait_mapping = nullptr;
      value.wait_state = nullptr;
      value.process_id = 0;
    }

    bool process_running_locked(state_t &value) {
      if (!value.process) {
        return false;
      }

      if (WaitForSingleObject(value.process, 0) == WAIT_TIMEOUT) {
        return true;
      }

      clear_process_locked(value);
      return false;
    }

    std::filesystem::path helper_path() {
      std::vector<wchar_t> executable_path(32768);
      const DWORD length = GetModuleFileNameW(nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
      if (length == 0 || length >= executable_path.size()) {
        return {};
      }

      return std::filesystem::path {executable_path.data(), executable_path.data() + length}.parent_path() /
             L"moonlight-latency-helper.exe";
    }

    HANDLE create_cross_session_event(const std::wstring &event_name) {
      // Sunshine can run as SYSTEM while the helper runs in the captured user's
      // session, so the named stop event must be openable across the session boundary.
      PSECURITY_DESCRIPTOR descriptor = nullptr;
      if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)",
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        BOOST_LOG(error) << "Failed to create latency benchmark event security descriptor: "sv << GetLastError();
        return nullptr;
      }

      SECURITY_ATTRIBUTES attributes {};
      attributes.nLength = sizeof(attributes);
      attributes.lpSecurityDescriptor = descriptor;
      attributes.bInheritHandle = FALSE;

      HANDLE event = CreateEventW(&attributes, TRUE, FALSE, event_name.c_str());
      const DWORD create_error = GetLastError();
      LocalFree(descriptor);

      if (!event) {
        BOOST_LOG(error) << "Failed to create latency benchmark event: "sv << create_error;
        return nullptr;
      }

      if (create_error == ERROR_ALREADY_EXISTS) {
        CloseHandle(event);
        return nullptr;
      }

      return event;
    }

    bool create_cross_session_wait_mapping(
      const std::wstring &mapping_name,
      HANDLE &mapping,
      shared_wait_state_t *&wait_state
    ) {
      PSECURITY_DESCRIPTOR descriptor = nullptr;
      if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)",
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
        BOOST_LOG(error) << "Failed to create latency benchmark mapping security descriptor: "sv << GetLastError();
        return false;
      }

      SECURITY_ATTRIBUTES attributes {};
      attributes.nLength = sizeof(attributes);
      attributes.lpSecurityDescriptor = descriptor;
      attributes.bInheritHandle = FALSE;

      mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        &attributes,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(shared_wait_state_t)),
        mapping_name.c_str()
      );
      const DWORD create_error = GetLastError();
      LocalFree(descriptor);

      if (!mapping || create_error == ERROR_ALREADY_EXISTS) {
        if (mapping) {
          CloseHandle(mapping);
          mapping = nullptr;
        }
        BOOST_LOG(error) << "Failed to create latency benchmark wait mapping: "sv << create_error;
        return false;
      }

      wait_state = static_cast<shared_wait_state_t *>(
        MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(shared_wait_state_t))
      );
      if (!wait_state) {
        const DWORD map_error = GetLastError();
        CloseHandle(mapping);
        mapping = nullptr;
        BOOST_LOG(error) << "Failed to map latency benchmark wait mapping: "sv << map_error;
        return false;
      }

      std::memset(wait_state, 0, sizeof(*wait_state));
      wait_state->magic = WAIT_MAP_MAGIC;
      wait_state->version = WAIT_MAP_VERSION;
      return true;
    }

    void terminate_process_locked(state_t &value) {
      if (!process_running_locked(value)) {
        return;
      }

      if (value.stop_event) {
        SetEvent(value.stop_event);
      }

      if (WaitForSingleObject(value.process, GRACEFUL_STOP_TIMEOUT_MS) == WAIT_TIMEOUT) {
        BOOST_LOG(warning) << "Latency benchmark helper did not exit after stop event; terminating it"sv;
        TerminateProcess(value.process, 1);
        WaitForSingleObject(value.process, FORCE_STOP_TIMEOUT_MS);
      }

      clear_process_locked(value);
    }

    void register_shutdown_cleanup() {
      static const bool registered = std::atexit([]() {
        (void) latency_benchmark::stop();
      }) == 0;
      (void) registered;
    }
  }  // namespace

  start_status_e start() {
    register_shutdown_cleanup();

    auto &value = state();
    std::scoped_lock lock {value.mutex};

    if (process_running_locked(value)) {
      return start_status_e::already_running;
    }

    const int active_sessions = rtsp_stream::session_count();
    if (active_sessions == 0) {
      return start_status_e::no_active_stream;
    }
    if (active_sessions != 1) {
      return start_status_e::busy;
    }

    const auto path = helper_path();
    if (path.empty() || !std::filesystem::is_regular_file(path)) {
      BOOST_LOG(error) << "Latency benchmark helper executable not found next to Sunshine"sv;
      return start_status_e::helper_missing;
    }

    const uint64_t event_serial = ++value.control_event_serial;
    const std::wstring object_prefix =
      L"Global\\SunshineLatencyBenchmark-" + std::to_wstring(GetCurrentProcessId()) +
      L"-" + std::to_wstring(event_serial);
    const std::wstring stop_event_name = object_prefix + L"-Stop";
    const std::wstring wait_mapping_name = object_prefix + L"-Wait";

    HANDLE stop_event = create_cross_session_event(stop_event_name);
    if (!stop_event) {
      return start_status_e::launch_failed;
    }

    HANDLE wait_mapping = nullptr;
    shared_wait_state_t *wait_state = nullptr;
    if (!create_cross_session_wait_mapping(wait_mapping_name, wait_mapping, wait_state)) {
      CloseHandle(stop_event);
      return start_status_e::launch_failed;
    }

    const std::string helper_utf8 = utf_utils::to_utf8(path.wstring());
    const std::string stop_event_utf8 = utf_utils::to_utf8(stop_event_name);
    const std::string wait_mapping_utf8 = utf_utils::to_utf8(wait_mapping_name);
    const int stream_fps = stream::session::active_framerate();
    if (stream_fps <= 0) {
      BOOST_LOG(error) << "Latency benchmark could not determine active stream frame rate"sv;
      UnmapViewOfFile(wait_state);
      CloseHandle(wait_mapping);
      CloseHandle(stop_event);
      return start_status_e::launch_failed;
    }
    const double helper_fps = stream_fps * 0.975;

    const std::string command_line =
      "\"" + helper_utf8 + "\" --fps " + std::to_string(helper_fps);

    boost::filesystem::path working_directory {path.parent_path().wstring()};
    auto environment = boost::this_process::environment();
    environment["SUNSHINE_LATENCY_STOP_EVENT"] = stop_event_utf8;
    environment["SUNSHINE_LATENCY_WAIT_MAP"] = wait_mapping_utf8;

    std::error_code launch_error;
    auto child = platf::run_command(
      false,
      true,
      command_line,
      working_directory,
      environment,
      nullptr,
      launch_error,
      nullptr
    );

    if (launch_error || !child.valid()) {
      UnmapViewOfFile(wait_state);
      CloseHandle(wait_mapping);
      CloseHandle(stop_event);
      BOOST_LOG(error) << "Failed to launch latency benchmark helper: "sv << launch_error.message();
      return start_status_e::launch_failed;
    }

    const DWORD child_id = static_cast<DWORD>(child.id());
    HANDLE process_handle = OpenProcess(
      SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
      FALSE,
      child_id
    );

    if (!process_handle) {
      const DWORD open_process_error = GetLastError();
      TerminateProcess(child.native_handle(), 1);
      child.detach();
      UnmapViewOfFile(wait_state);
      CloseHandle(wait_mapping);
      CloseHandle(stop_event);
      BOOST_LOG(error) << "Failed to retain latency benchmark helper process handle: "sv << open_process_error;
      return start_status_e::launch_failed;
    }

    child.detach();

    value.process = process_handle;
    value.stop_event = stop_event;
    value.wait_mapping = wait_mapping;
    value.wait_state = wait_state;
    value.process_id = child_id;

    // START is intentionally only a launch request. Moonlight does not depend on
    // this HTTP response for readiness; it waits a fixed startup interval and
    // validates the helper through an actual marker transition in the video path.
    BOOST_LOG(info) << "Latency benchmark helper launched (PID "sv << value.process_id << ')';
    return start_status_e::started;
  }

  sample_status_e sample(uint64_t sequence, uint64_t &wait_us) {
    auto &value = state();
    std::scoped_lock lock {value.mutex};

    if (!process_running_locked(value) || !value.wait_state ||
        value.wait_state->magic != WAIT_MAP_MAGIC ||
        value.wait_state->version != WAIT_MAP_VERSION ||
        sequence == 0) {
      return sample_status_e::not_available;
    }

    wait_record_t &record = value.wait_state->records[sequence % WAIT_RECORD_COUNT];
    const LONG64 sequence_before = InterlockedCompareExchange64(&record.sequence, 0, 0);
    if (sequence_before != static_cast<LONG64>(sequence)) {
      return sample_status_e::not_available;
    }

    const LONG64 wait_value = InterlockedCompareExchange64(&record.wait_us, 0, 0);
    MemoryBarrier();
    const LONG64 sequence_after = InterlockedCompareExchange64(&record.sequence, 0, 0);
    if (sequence_after != sequence_before || wait_value < 0) {
      return sample_status_e::not_available;
    }

    wait_us = static_cast<uint64_t>(wait_value);
    return sample_status_e::ready;
  }

  stop_status_e stop() {
    auto &value = state();
    std::scoped_lock lock {value.mutex};

    if (!process_running_locked(value)) {
      return stop_status_e::not_running;
    }

    terminate_process_locked(value);
    BOOST_LOG(info) << "Latency benchmark helper stopped"sv;
    return stop_status_e::stopped;
  }
}  // namespace latency_benchmark

#endif  // _WIN32
