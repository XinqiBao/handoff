#pragma once

#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace handoff::bench {

struct AssetDescriptor {
  std::string_view name;
  std::string_view note;
};

enum class RouteRole { single, mpsc, shared_consumers, fan_out, pipeline };
enum class CapacityKind { slots, bytes, slots_and_bytes };

struct RouteDescriptor {
  std::string_view name;
  Implementation implementation;
  std::string_view asset;
  std::uint8_t workloads;
  Benchmark default_workload;
  RouteRole role;
  CapacityKind capacity;
  bool benchmark_control{false};
};

inline constexpr std::uint8_t throughput_workload = 1;
inline constexpr std::uint8_t ping_pong_workload = 2;
inline constexpr std::uint8_t offered_load_workload = 4;
inline constexpr std::uint8_t publication_hole_workload = 8;
inline constexpr std::uint8_t ordinary_workloads = throughput_workload | ping_pong_workload;
inline constexpr std::uint8_t mpsc_workloads = throughput_workload | publication_hole_workload;

inline constexpr std::array assets{
    AssetDescriptor{"basic-bounded-spsc", "docs/mechanisms/basic-bounded-spsc.md"},
    AssetDescriptor{"cache-line-bounded-spsc", "docs/mechanisms/cache-line-bounded-spsc.md"},
    AssetDescriptor{"cached-index-bounded-spsc", "docs/mechanisms/cached-index-bounded-spsc.md"},
    AssetDescriptor{"batch-bounded-spsc", "docs/mechanisms/batch-bounded-spsc.md"},
    AssetDescriptor{"bulk-burst-bounded-spsc", "docs/mechanisms/bulk-burst-bounded-spsc.md"},
    AssetDescriptor{"staged-bounded-spsc", "docs/mechanisms/staged-bounded-spsc.md"},
    AssetDescriptor{"bounded-sequence-ring", "docs/mechanisms/bounded-sequence-ring.md"},
    AssetDescriptor{"ordered-publication-mpsc", "docs/mechanisms/ordered-publication-mpsc.md"},
    AssetDescriptor{"completion-count-mpsc", "docs/mechanisms/completion-count-mpsc.md"},
    AssetDescriptor{"slot-availability-mpsc", "docs/mechanisms/slot-availability-mpsc.md"},
    AssetDescriptor{"serialized-consumer-spmc", "docs/mechanisms/serialized-consumer-spmc.md"},
    AssetDescriptor{"ordered-release-spmc", "docs/mechanisms/ordered-release-spmc.md"},
    AssetDescriptor{"slot-completion-spmc", "docs/mechanisms/slot-completion-spmc.md"},
    AssetDescriptor{"two-path-merge", "docs/mechanisms/two-path-merge.md"},
    AssetDescriptor{"bounded-sequence-fan-out", "docs/mechanisms/bounded-sequence-fan-out.md"},
    AssetDescriptor{"bounded-sequence-pipeline", "docs/mechanisms/bounded-sequence-pipeline.md"},
    AssetDescriptor{"ordered-worker-stage", "docs/mechanisms/ordered-worker-stage.md"},
    AssetDescriptor{"two-branch-join", "docs/mechanisms/two-branch-join.md"},
    AssetDescriptor{"sequence-metadata-ring", "docs/mechanisms/sequence-metadata-ring.md"},
    AssetDescriptor{"sequence-payload-ring", "docs/mechanisms/sequence-payload-ring.md"},
    AssetDescriptor{"fixed-record-spsc", "docs/mechanisms/fixed-record-spsc.md"},
    AssetDescriptor{"variable-record-spsc", "docs/mechanisms/variable-record-spsc.md"},
    AssetDescriptor{"descriptor-payload-spsc", "docs/mechanisms/descriptor-payload-spsc.md"},
    AssetDescriptor{"mpsc-variable-record", "docs/mechanisms/mpsc-variable-record.md"},
};

inline constexpr std::array routes{
    RouteDescriptor{"basic", Implementation::basic, "basic-bounded-spsc", ordinary_workloads,
                    Benchmark::throughput, RouteRole::single, CapacityKind::slots},
    RouteDescriptor{"cache-line", Implementation::cache_line, "cache-line-bounded-spsc",
                    ordinary_workloads, Benchmark::throughput, RouteRole::single,
                    CapacityKind::slots},
    RouteDescriptor{"cached-index", Implementation::cached_index, "cached-index-bounded-spsc",
                    ordinary_workloads, Benchmark::throughput, RouteRole::single,
                    CapacityKind::slots},
    RouteDescriptor{"batch", Implementation::batch, "batch-bounded-spsc", ordinary_workloads,
                    Benchmark::throughput, RouteRole::single, CapacityKind::slots},
    RouteDescriptor{"bulk", Implementation::bulk, "bulk-burst-bounded-spsc", throughput_workload,
                    Benchmark::throughput, RouteRole::single, CapacityKind::slots},
    RouteDescriptor{"burst", Implementation::burst, "bulk-burst-bounded-spsc", throughput_workload,
                    Benchmark::throughput, RouteRole::single, CapacityKind::slots},
    RouteDescriptor{"staged", Implementation::staged, "staged-bounded-spsc", throughput_workload,
                    Benchmark::throughput, RouteRole::single, CapacityKind::slots},
    RouteDescriptor{"sequence", Implementation::sequence, "bounded-sequence-ring",
                    ordinary_workloads, Benchmark::throughput, RouteRole::single,
                    CapacityKind::slots},
    RouteDescriptor{"mpsc-ordered", Implementation::mpsc_ordered, "ordered-publication-mpsc",
                    mpsc_workloads, Benchmark::throughput, RouteRole::mpsc, CapacityKind::slots},
    RouteDescriptor{"mpsc-count", Implementation::mpsc_count, "completion-count-mpsc",
                    mpsc_workloads, Benchmark::throughput, RouteRole::mpsc, CapacityKind::slots},
    RouteDescriptor{"mpsc-slot", Implementation::mpsc_slot, "slot-availability-mpsc",
                    mpsc_workloads, Benchmark::throughput, RouteRole::mpsc, CapacityKind::slots},
    RouteDescriptor{"mpsc-serialized", Implementation::mpsc_serialized, "basic-bounded-spsc",
                    throughput_workload, Benchmark::throughput, RouteRole::mpsc,
                    CapacityKind::slots, true},
    RouteDescriptor{"spmc-serialized", Implementation::spmc_serialized, "serialized-consumer-spmc",
                    throughput_workload, Benchmark::throughput, RouteRole::shared_consumers,
                    CapacityKind::slots},
    RouteDescriptor{"spmc-ordered", Implementation::spmc_ordered, "ordered-release-spmc",
                    throughput_workload, Benchmark::throughput, RouteRole::shared_consumers,
                    CapacityKind::slots},
    RouteDescriptor{"spmc-slot", Implementation::spmc_slot, "slot-completion-spmc",
                    throughput_workload, Benchmark::throughput, RouteRole::shared_consumers,
                    CapacityKind::slots},
    RouteDescriptor{"fan-out", Implementation::fan_out, "bounded-sequence-fan-out",
                    throughput_workload, Benchmark::throughput, RouteRole::fan_out,
                    CapacityKind::slots},
    RouteDescriptor{"pipeline", Implementation::pipeline, "bounded-sequence-pipeline",
                    throughput_workload, Benchmark::throughput, RouteRole::pipeline,
                    CapacityKind::slots},
    RouteDescriptor{"sequence-payload", Implementation::sequence_payload, "sequence-payload-ring",
                    offered_load_workload, Benchmark::offered_load, RouteRole::single,
                    CapacityKind::slots},
    RouteDescriptor{"fixed-record", Implementation::fixed_record, "fixed-record-spsc",
                    ordinary_workloads, Benchmark::throughput, RouteRole::single,
                    CapacityKind::slots},
    RouteDescriptor{"byte-record", Implementation::byte_record, "variable-record-spsc",
                    ordinary_workloads, Benchmark::throughput, RouteRole::single,
                    CapacityKind::bytes},
    RouteDescriptor{"descriptor-record", Implementation::descriptor_record,
                    "descriptor-payload-spsc", ordinary_workloads, Benchmark::throughput,
                    RouteRole::single, CapacityKind::slots_and_bytes},
};

consteval bool catalog_consistent() {
  for (std::size_t index = 0; index < routes.size(); ++index) {
    const auto& route = routes[index];
    bool asset_found = false;
    for (const auto& asset : assets) {
      asset_found |= route.asset == asset.name;
      if (route.name == asset.name) {
        return false;
      }
    }
    if (!asset_found || route.name == "smoke" || route.name == "throughput" ||
        route.name == "ping-pong" || route.name == "offered-load" ||
        route.name == "publication-hole") {
      return false;
    }
    std::uint8_t default_flag = 0;
    switch (route.default_workload) {
    case Benchmark::smoke:
      return false;
    case Benchmark::throughput:
      default_flag = throughput_workload;
      break;
    case Benchmark::ping_pong:
      default_flag = ping_pong_workload;
      break;
    case Benchmark::offered_load:
      default_flag = offered_load_workload;
      break;
    case Benchmark::publication_hole:
      default_flag = publication_hole_workload;
      break;
    }
    if ((route.workloads & default_flag) == 0) {
      return false;
    }
    for (std::size_t other = index + 1; other < routes.size(); ++other) {
      if (route.name == routes[other].name ||
          route.implementation == routes[other].implementation) {
        return false;
      }
    }
  }
  return true;
}

static_assert(catalog_consistent(), "benchmark routes must name distinct assets and defaults");

const AssetDescriptor* find_asset(std::string_view name);
const RouteDescriptor* find_route(std::string_view name);
const RouteDescriptor& route_for(Implementation implementation);
std::string_view benchmark_name(Benchmark benchmark);
bool supports(const RouteDescriptor& route, Benchmark benchmark);

} // namespace handoff::bench
