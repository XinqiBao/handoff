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

Options exploratory_options(const RouteDescriptor& route) {
  Options options;
  options.implementation = route.implementation;
  options.iterations = 10'000;
  options.warmup = 100;
  options.trials = 1;
  options.payload_bytes = 8;
  options.capacity_slots = 64;
  if (route.capacity != CapacityKind::slots) {
    options.capacity_bytes = 4'096;
  }
  return options;
}

} // namespace handoff::bench
