#include "catalog.hpp"

#include <stdexcept>

namespace handoff::bench {

const AssetDescriptor* find_asset(std::string_view name) {
  for (const auto& asset : assets) {
    if (asset.name == name) {
      return &asset;
    }
  }
  return nullptr;
}

const RouteDescriptor* find_route(std::string_view name) {
  for (const auto& route : routes) {
    if (route.name == name) {
      return &route;
    }
  }
  return nullptr;
}

const RouteDescriptor& route_for(Implementation implementation) {
  for (const auto& route : routes) {
    if (route.implementation == implementation) {
      return route;
    }
  }
  throw std::logic_error("unknown implementation");
}

std::string_view benchmark_name(Benchmark benchmark) {
  switch (benchmark) {
  case Benchmark::smoke:
    return "smoke";
  case Benchmark::throughput:
    return "throughput";
  case Benchmark::ping_pong:
    return "ping-pong";
  case Benchmark::offered_load:
    return "offered-load";
  case Benchmark::publication_hole:
    return "publication-hole";
  }
  throw std::logic_error("unknown benchmark");
}

bool supports(const RouteDescriptor& route, Benchmark benchmark) {
  std::uint8_t flag = 0;
  switch (benchmark) {
  case Benchmark::smoke:
    return false;
  case Benchmark::throughput:
    flag = throughput_workload;
    break;
  case Benchmark::ping_pong:
    flag = ping_pong_workload;
    break;
  case Benchmark::offered_load:
    flag = offered_load_workload;
    break;
  case Benchmark::publication_hole:
    flag = publication_hole_workload;
    break;
  }
  return (route.workloads & flag) != 0;
}

} // namespace handoff::bench
