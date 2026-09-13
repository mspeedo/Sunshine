#pragma once

#ifdef _WIN32

#include "nvhttp.h"

#include <memory>

namespace nvhttp {
  void latency_benchmark_control(
    std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Response> response,
    std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Request> request
  );
}  // namespace nvhttp

#endif  // _WIN32
