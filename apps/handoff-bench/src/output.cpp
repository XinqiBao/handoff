#include "output.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace handoff::bench {
namespace {

std::optional<double> median(std::vector<double> values) {
  if (values.empty()) {
    return std::nullopt;
  }
  std::ranges::sort(values);
  const auto middle = values.size() / 2;
  if (values.size() % 2 == 1) {
    return {values[middle]};
  }
  return {(values[middle - 1] + values[middle]) / 2.0};
}

std::string_view benchmark_name(Benchmark benchmark) {
  switch (benchmark) {
  case Benchmark::smoke:
    return "smoke";
  case Benchmark::throughput:
    return "throughput";
  case Benchmark::ping_pong:
    return "ping-pong";
  }
  throw std::logic_error("unknown benchmark");
}

std::string_view implementation_name(Implementation implementation) {
  switch (implementation) {
  case Implementation::basic:
    return "basic";
  case Implementation::cache_line:
    return "cache-line";
  case Implementation::cached_index:
    return "cached-index";
  }
  throw std::logic_error("unknown implementation");
}

std::string optional_cpu_value(std::optional<unsigned int> cpu, std::string_view unavailable) {
  if (!cpu) {
    return std::string(unavailable);
  }
  return std::to_string(*cpu);
}

std::string_view affinity_outcome(const PlacementResult& placement) {
  if (!placement.requested) {
    return "not-requested";
  }
  switch (placement.outcome.status) {
  case platform::AffinityStatus::applied:
    return "applied";
  case platform::AffinityStatus::unsupported:
    return "unsupported";
  case platform::AffinityStatus::invalid_cpu:
    return "invalid-cpu";
  case platform::AffinityStatus::system_error:
    return "system-error";
  }
  throw std::logic_error("unknown affinity outcome");
}

std::string optional_string_value(const std::optional<std::string>& value) {
  return value.value_or("unavailable");
}

std::string optional_bool_value(const std::optional<bool>& value) {
  if (!value) {
    return "unavailable";
  }
  return *value ? "true" : "false";
}

void print_placement(std::string_view role, const PlacementResult& placement) {
  if (!placement.requested) {
    return;
  }
  std::cout << role << " CPU " << *placement.requested << ": " << placement.outcome.message << '\n';
}

} // namespace

bool write_csv(const std::filesystem::path& path, Benchmark benchmark, const Options& options,
               const RunResults& results, const RunMetadata& metadata) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "unable to open output file: " << path << '\n';
    return false;
  }

  output << "# git_revision=" << optional_string_value(metadata.git_revision) << '\n'
         << "# git_dirty=" << optional_bool_value(metadata.git_dirty) << '\n'
         << "# compiler=" << metadata.system.compiler << '\n'
         << "# compiler_version=" << metadata.system.compiler_version << '\n'
         << "# build_mode=" << metadata.build_mode << '\n'
         << "# operating_system=" << metadata.system.operating_system << '\n'
         << "# architecture=" << metadata.system.architecture << '\n'
         << "# cpu_model=" << metadata.system.cpu_model << '\n'
         << "# waiting_behavior=" << metadata.waiting_behavior << '\n'
         << "# warmup=" << options.warmup << '\n'
         << "# trials=" << options.trials << '\n';
  if (benchmark != Benchmark::smoke) {
    output << "# producer_cpu_requested="
           << optional_cpu_value(results.producer_placement.requested, "not-requested") << '\n'
           << "# producer_cpu_effective="
           << optional_cpu_value(results.producer_placement.effective, "unavailable") << '\n'
           << "# producer_affinity_outcome=" << affinity_outcome(results.producer_placement) << '\n'
           << "# consumer_cpu_requested="
           << optional_cpu_value(results.consumer_placement.requested, "not-requested") << '\n'
           << "# consumer_cpu_effective="
           << optional_cpu_value(results.consumer_placement.effective, "unavailable") << '\n'
           << "# consumer_affinity_outcome=" << affinity_outcome(results.consumer_placement)
           << '\n';
  }
  output << "benchmark,implementation,payload_bytes,capacity_slots,capacity_bytes,batch_size,"
            "iterations,trial,elapsed_ns,messages_per_second,latency_ns,latency_p95_ns,"
            "latency_p99_ns,checksum\n";
  for (const auto& result : results.trials) {
    if (benchmark == Benchmark::smoke) {
      output << "smoke,harness,,,,," << options.iterations << ',' << result.trial << ','
             << result.elapsed_ns << ",,,,," << result.checksum << '\n';
    } else {
      output << benchmark_name(benchmark) << ',' << implementation_name(options.implementation)
             << ',' << options.payload_bytes << ',' << options.capacity_slots << ',' << ",,"
             << options.iterations << ',' << result.trial << ',' << result.elapsed_ns << ',';
      if (benchmark == Benchmark::throughput) {
        if (result.messages_per_second) {
          output << std::fixed << std::setprecision(3) << *result.messages_per_second;
        }
        output << ",,,,";
      } else {
        output << ',' << std::fixed << std::setprecision(3);
        if (result.latency_ns) {
          output << *result.latency_ns;
        }
        output << ',';
        if (result.latency_p95_ns) {
          output << *result.latency_p95_ns;
        }
        output << ',';
        if (result.latency_p99_ns) {
          output << *result.latency_p99_ns;
        }
        output << ',';
      }
      output << result.checksum << '\n';
    }
  }
  output.flush();
  if (!output) {
    std::cerr << "unable to write output file: " << path << '\n';
    return false;
  }
  return true;
}

void print_results(Benchmark benchmark, const Options& options, const RunResults& results,
                   const RunMetadata& metadata) {
  std::cout << benchmark_name(benchmark);
  if (benchmark == Benchmark::smoke) {
    std::cout << " (harness plumbing only; not a handoff benchmark)";
  } else {
    std::cout << " / " << implementation_name(options.implementation) << " / "
              << options.payload_bytes << " B / " << options.capacity_slots << " slots";
  }
  std::cout << "\nsystem: " << metadata.system.operating_system << ", "
            << metadata.system.architecture << ", " << metadata.system.compiler << ' '
            << metadata.system.compiler_version << '\n';
  print_placement("producer", results.producer_placement);
  print_placement("consumer", results.consumer_placement);

  std::vector<double> summaries;
  summaries.reserve(results.trials.size());
  std::cout << std::fixed;
  for (const auto& result : results.trials) {
    std::cout << "trial " << result.trial << ": " << result.elapsed_ns << " ns, ";
    if (benchmark == Benchmark::ping_pong) {
      if (result.latency_ns && result.latency_p95_ns && result.latency_p99_ns) {
        std::cout << std::setprecision(3) << *result.latency_ns << " ns median RTT, p95 "
                  << *result.latency_p95_ns << " ns, p99 " << *result.latency_p99_ns << " ns, "
                  << *result.latency_ns / 2.0 << " ns median RTT/2 proxy";
        summaries.push_back(*result.latency_ns);
      } else {
        std::cout << "latency unavailable";
      }
    } else {
      if (result.messages_per_second) {
        std::cout << std::setprecision(0) << *result.messages_per_second
                  << (benchmark == Benchmark::smoke ? " operations/s" : " messages/s");
        summaries.push_back(*result.messages_per_second);
      } else {
        std::cout << "rate unavailable (zero elapsed duration)";
      }
    }
    std::cout << ", checksum " << result.checksum << '\n';
  }

  if (benchmark == Benchmark::ping_pong) {
    const auto median_rtt = median(std::move(summaries));
    if (median_rtt) {
      std::cout << std::setprecision(3) << "median of trial medians: " << *median_rtt << " ns RTT, "
                << *median_rtt / 2.0 << " ns RTT/2 proxy\n";
    } else {
      std::cout << "median of trial medians: unavailable\n";
    }
  } else {
    const auto median_rate = median(std::move(summaries));
    if (median_rate) {
      std::cout << std::setprecision(0) << "median: " << *median_rate
                << (benchmark == Benchmark::smoke ? " operations/s\n" : " messages/s\n");
    } else {
      std::cout << "median: unavailable\n";
    }
  }
}

} // namespace handoff::bench
