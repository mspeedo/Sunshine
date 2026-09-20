#include "latency_benchmark_http.h"

#ifdef _WIN32

#include "latency_benchmark.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

using namespace std::literals;

namespace nvhttp {
  void latency_benchmark_control(
    std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Response> response,
    std::shared_ptr<typename SimpleWeb::ServerBase<SunshineHTTPS>::Request> request
  ) {
    namespace pt = boost::property_tree;

    pt::ptree tree;
    const auto write_response = [&]() {
      std::ostringstream data;
      pt::write_xml(data, tree);
      response->write(data.str());
      response->close_connection_after_response = true;
    };

    const auto args = request->parse_query_string();
    const auto action_it = args.find("action");
    if (action_it == std::end(args)) {
      tree.put("root.<xmlattr>.status_code", 400);
      tree.put("root.<xmlattr>.status_message", "Missing latency benchmark action");
      write_response();
      return;
    }

    if (action_it->second == "start"sv) {
      const auto status = latency_benchmark::start();
      switch (status) {
        case latency_benchmark::start_status_e::started:
        case latency_benchmark::start_status_e::already_running:
          tree.put("root.<xmlattr>.status_code", 200);
          tree.put("root.latencybenchmark", "ready");
          break;
        case latency_benchmark::start_status_e::busy:
          tree.put("root.<xmlattr>.status_code", 409);
          tree.put("root.<xmlattr>.status_message", "Latency benchmark requires exactly one active stream");
          break;
        case latency_benchmark::start_status_e::no_active_stream:
          tree.put("root.<xmlattr>.status_code", 409);
          tree.put("root.<xmlattr>.status_message", "No active stream for latency benchmark");
          break;
        case latency_benchmark::start_status_e::helper_missing:
          tree.put("root.<xmlattr>.status_code", 503);
          tree.put("root.<xmlattr>.status_message", "Latency benchmark helper executable is missing");
          break;
        case latency_benchmark::start_status_e::launch_failed:
          tree.put("root.<xmlattr>.status_code", 503);
          tree.put("root.<xmlattr>.status_message", "Latency benchmark helper failed to start");
          break;
        case latency_benchmark::start_status_e::unsupported:
          tree.put("root.<xmlattr>.status_code", 501);
          tree.put("root.<xmlattr>.status_message", "Latency benchmark helper is unsupported on this platform");
          break;
      }

      write_response();
      return;
    }

    if (action_it->second == "sample"sv) {
      const auto sequence_it = args.find("sequence");
      if (sequence_it == std::end(args)) {
        tree.put("root.<xmlattr>.status_code", 400);
        tree.put("root.<xmlattr>.status_message", "Missing latency benchmark sequence");
        write_response();
        return;
      }

      uint64_t sequence = 0;
      try {
        sequence = std::stoull(sequence_it->second);
      } catch (const std::exception &) {
        tree.put("root.<xmlattr>.status_code", 400);
        tree.put("root.<xmlattr>.status_message", "Invalid latency benchmark sequence");
        write_response();
        return;
      }

      uint64_t wait_us = 0;
      tree.put("root.<xmlattr>.status_code", 200);
      if (latency_benchmark::sample(sequence, wait_us) ==
          latency_benchmark::sample_status_e::ready) {
        tree.put("root.latencybenchmark", "sample");
        tree.put("root.sequence", sequence);
        tree.put("root.wait_us", wait_us);
      } else {
        tree.put("root.latencybenchmark", "missing");
        tree.put("root.sequence", sequence);
      }

      write_response();
      return;
    }

    if (action_it->second == "stop"sv) {
      const auto status = latency_benchmark::stop();
      switch (status) {
        case latency_benchmark::stop_status_e::stopped:
        case latency_benchmark::stop_status_e::not_running:
          tree.put("root.<xmlattr>.status_code", 200);
          tree.put("root.latencybenchmark", "stopped");
          break;
      }

      write_response();
      return;
    }

    tree.put("root.<xmlattr>.status_code", 400);
    tree.put("root.<xmlattr>.status_message", "Unknown latency benchmark action");
    write_response();
  }
}  // namespace nvhttp

#endif  // _WIN32
