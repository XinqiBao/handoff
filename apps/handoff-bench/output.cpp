#include "benchmark.hpp"

#include "handoff/platform/system_info.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace handoff::bench {
namespace {

double median(std::vector<double> values) {
  std::ranges::sort(values);
  const auto middle = values.size() / 2;
  if (values.size() % 2 == 1) {
    return values[middle];
  }
  return (values[middle - 1] + values[middle]) / 2.0;
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
  }
  throw std::logic_error("unknown implementation");
}

std::string placement_value(const PlacementResult& placement) {
  if (!placement.requested) {
    return "not-requested";
  }
  if (placement.outcome.status == platform::AffinityStatus::applied) {
    return std::to_string(*placement.requested);
  }
  return "unsupported";
}

void print_placement(std::string_view role, const PlacementResult& placement) {
  if (!placement.requested) {
    return;
  }
  std::cout << role << " CPU " << *placement.requested << ": " << placement.outcome.message << '\n';
}

} // namespace

bool write_csv(const std::filesystem::path& path, Benchmark benchmark, const Options& options,
               const RunResults& results) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "unable to open output file: " << path << '\n';
    return false;
  }

  const auto info = platform::current_system_info();
  output << "# operating_system=" << info.operating_system << '\n'
         << "# architecture=" << info.architecture << '\n'
         << "# compiler=" << info.compiler << '\n'
         << "# warmup=" << options.warmup << '\n'
         << "# trials=" << options.trials << '\n';
  if (benchmark != Benchmark::smoke) {
    output << "# producer_cpu=" << placement_value(results.producer_placement) << '\n'
           << "# consumer_cpu=" << placement_value(results.consumer_placement) << '\n';
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
             << ',' << options.payload_bytes << ',' << options.capacity_slots << ','
             << options.payload_bytes * options.capacity_slots << ",," << options.iterations << ','
             << result.trial << ',' << result.elapsed_ns << ',';
      if (benchmark == Benchmark::throughput) {
        output << std::fixed << std::setprecision(3) << result.messages_per_second << ",,,,";
      } else {
        output << ',' << std::fixed << std::setprecision(3) << result.latency_ns << ','
               << result.latency_p95_ns << ',' << result.latency_p99_ns << ',';
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

void print_results(Benchmark benchmark, const Options& options, const RunResults& results) {
  const auto info = platform::current_system_info();
  std::cout << benchmark_name(benchmark);
  if (benchmark == Benchmark::smoke) {
    std::cout << " (harness plumbing only; not a handoff benchmark)";
  } else {
    std::cout << " / " << implementation_name(options.implementation) << " / "
              << options.payload_bytes << " B / " << options.capacity_slots << " slots";
  }
  std::cout << "\nsystem: " << info.operating_system << ", " << info.architecture << ", "
            << info.compiler << '\n';
  print_placement("producer", results.producer_placement);
  print_placement("consumer", results.consumer_placement);

  std::vector<double> summaries;
  summaries.reserve(results.trials.size());
  std::cout << std::fixed;
  for (const auto& result : results.trials) {
    std::cout << "trial " << result.trial << ": " << result.elapsed_ns << " ns, ";
    if (benchmark == Benchmark::ping_pong) {
      std::cout << std::setprecision(3) << result.latency_ns << " ns median RTT, p95 "
                << result.latency_p95_ns << " ns, p99 " << result.latency_p99_ns << " ns, "
                << result.latency_ns / 2.0 << " ns median RTT/2 proxy";
      summaries.push_back(result.latency_ns);
    } else {
      std::cout << std::setprecision(0) << result.messages_per_second
                << (benchmark == Benchmark::smoke ? " operations/s" : " messages/s");
      summaries.push_back(result.messages_per_second);
    }
    std::cout << ", checksum " << result.checksum << '\n';
  }

  if (benchmark == Benchmark::ping_pong) {
    const auto median_rtt = median(std::move(summaries));
    std::cout << std::setprecision(3) << "median of trial medians: " << median_rtt << " ns RTT, "
              << median_rtt / 2.0 << " ns RTT/2 proxy\n";
  } else {
    std::cout << std::setprecision(0) << "median: " << median(std::move(summaries))
              << (benchmark == Benchmark::smoke ? " operations/s\n" : " messages/s\n");
  }
}

} // namespace handoff::bench
