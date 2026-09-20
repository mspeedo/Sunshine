#pragma once

#include <cstdint>

namespace latency_benchmark {
  enum class start_status_e {
    started,
    already_running,
    busy,
    no_active_stream,
    helper_missing,
    launch_failed,
    unsupported,
  };

  enum class stop_status_e {
    stopped,
    not_running,
  };

  enum class sample_status_e {
    ready,
    not_available,
  };

#ifdef _WIN32
  start_status_e start();
  sample_status_e sample(uint64_t sequence, uint64_t &wait_us);
  stop_status_e stop();
#else
  inline start_status_e start() {
    return start_status_e::unsupported;
  }

  inline sample_status_e sample(uint64_t, uint64_t &) {
    return sample_status_e::not_available;
  }

  inline stop_status_e stop() {
    return stop_status_e::not_running;
  }
#endif
}  // namespace latency_benchmark
