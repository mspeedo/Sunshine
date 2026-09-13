#pragma once

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

#ifdef _WIN32
  start_status_e start();
  stop_status_e stop();
#else
  inline start_status_e start() {
    return start_status_e::unsupported;
  }

  inline stop_status_e stop() {
    return stop_status_e::not_running;
  }
#endif
}  // namespace latency_benchmark
