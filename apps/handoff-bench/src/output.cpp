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
  case Benchmark::offered_load:
    return "offered-load";
  }
  throw std::logic_error("unknown benchmark");
}

std::string_view implementation_name(Implementation implementation) {
  switch (implementation) {
  case Implementation::basic:
    return "basic";
  case Implementation::batch:
    return "batch";
  case Implementation::bulk:
    return "bulk";
  case Implementation::burst:
    return "burst";
  case Implementation::byte_record:
    return "byte-record";
  case Implementation::cache_line:
    return "cache-line";
  case Implementation::cached_index:
    return "cached-index";
  case Implementation::descriptor_record:
    return "descriptor-record";
  case Implementation::fan_out:
    return "fan-out";
  case Implementation::fixed_record:
    return "fixed-record";
  case Implementation::pipeline:
    return "pipeline";
  case Implementation::sequence:
    return "sequence";
  case Implementation::sequence_payload:
    return "sequence-payload";
  case Implementation::staged:
    return "staged";
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

std::size_t required_byte_capacity(const Options& options) {
  const auto capacity = options.capacity_bytes;
  if (!capacity) {
    throw std::logic_error("byte-record options require byte capacity");
  }
  return *capacity;
}

void write_empty_fields(std::ostream& output, std::size_t count) {
  for (std::size_t index = 0; index < count; ++index) {
    output << ',';
  }
}

template <typename Value>
void write_optional(std::ostream& output, const std::optional<Value>& value) {
  if (value) {
    output << *value;
  }
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
           << "# producer_affinity_outcome=" << affinity_outcome(results.producer_placement)
           << '\n';
    if (options.implementation == Implementation::fan_out ||
        options.implementation == Implementation::pipeline) {
      const auto consumer_count = options.implementation == Implementation::fan_out
                                      ? fan_out_consumer_count
                                      : pipeline_consumer_count;
      output << "# consumer_count=" << consumer_count << '\n'
             << "# consumer_cpus_requested=not-requested\n"
             << "# consumer_cpus_effective=unavailable\n"
             << "# consumer_affinity_outcome=not-requested\n";
    } else {
      output << "# consumer_cpu_requested="
             << optional_cpu_value(results.consumer_placement.requested, "not-requested") << '\n'
             << "# consumer_cpu_effective="
             << optional_cpu_value(results.consumer_placement.effective, "unavailable") << '\n'
             << "# consumer_affinity_outcome=" << affinity_outcome(results.consumer_placement)
             << '\n';
    }
  }
  if (benchmark == Benchmark::offered_load) {
    output << "# producer_interval_ns=" << options.producer_interval_ns << '\n'
           << "# consumer_stall_every=" << options.consumer_stall_every << '\n'
           << "# consumer_stall_ns=" << options.consumer_stall_ns << '\n';
  }
  output << "benchmark,implementation,payload_bytes,capacity_slots,capacity_bytes,batch_size,"
            "iterations,trial,elapsed_ns,messages_per_second,latency_ns,latency_p95_ns,"
            "latency_p99_ns,checksum,producer_interval_ns,consumer_stall_every,consumer_stall_ns,"
            "offered_messages,observed_messages,overwritten_messages,retry_attempts,"
            "observed_payload_bytes,offered_messages_per_second,observed_messages_per_second\n";
  for (const auto& result : results.trials) {
    if (benchmark == Benchmark::smoke) {
      output << "smoke,harness,,,,," << options.iterations << ',' << result.trial << ','
             << result.elapsed_ns << ",,,,," << result.checksum;
      write_empty_fields(output, 10);
      output << '\n';
    } else {
      output << benchmark_name(benchmark) << ',' << implementation_name(options.implementation)
             << ',' << options.payload_bytes << ',';
      if (options.implementation == Implementation::byte_record) {
        output << ',' << required_byte_capacity(options) << ',';
      } else if (options.implementation == Implementation::descriptor_record) {
        output << options.capacity_slots << ',' << required_byte_capacity(options) << ',';
      } else {
        output << options.capacity_slots << ",,";
      }
      if (benchmark == Benchmark::throughput) {
        output << options.batch_size;
      }
      output << ',' << options.iterations << ',' << result.trial << ',' << result.elapsed_ns << ',';
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
      output << result.checksum;
      if (benchmark == Benchmark::offered_load) {
        output << ',' << options.producer_interval_ns << ',' << options.consumer_stall_every << ','
               << options.consumer_stall_ns << ',';
        write_optional(output, result.offered_messages);
        output << ',';
        write_optional(output, result.observed_messages);
        output << ',';
        write_optional(output, result.overwritten_messages);
        output << ',';
        write_optional(output, result.retry_attempts);
        output << ',';
        write_optional(output, result.observed_payload_bytes);
        output << ',' << std::fixed << std::setprecision(3);
        write_optional(output, result.offered_messages_per_second);
        output << ',';
        write_optional(output, result.observed_messages_per_second);
      } else {
        write_empty_fields(output, 10);
      }
      output << '\n';
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
              << options.payload_bytes << " B / ";
    if (options.implementation == Implementation::byte_record) {
      std::cout << required_byte_capacity(options) << " bytes";
    } else if (options.implementation == Implementation::descriptor_record) {
      std::cout << options.capacity_slots << " slots / " << required_byte_capacity(options)
                << " bytes";
    } else {
      std::cout << options.capacity_slots << " slots";
    }
    if (benchmark == Benchmark::throughput) {
      std::cout << " / batch " << options.batch_size;
      if (options.implementation == Implementation::fan_out) {
        std::cout << " / " << fan_out_consumer_count << " consumers";
      } else if (options.implementation == Implementation::pipeline) {
        std::cout << " / " << pipeline_consumer_count << " stages";
      }
    } else if (benchmark == Benchmark::offered_load) {
      std::cout << " / producer interval " << options.producer_interval_ns << " ns";
      if (options.consumer_stall_every == 0) {
        std::cout << " / consumer stall disabled";
      } else {
        std::cout << " / consumer stall every " << options.consumer_stall_every << " observations"
                  << " for " << options.consumer_stall_ns << " ns";
      }
    }
  }
  std::cout << "\nsystem: " << metadata.system.operating_system << ", "
            << metadata.system.architecture << ", " << metadata.system.compiler << ' '
            << metadata.system.compiler_version << '\n';
  print_placement("producer", results.producer_placement);
  print_placement("consumer", results.consumer_placement);

  std::vector<double> summaries;
  std::vector<double> observed_summaries;
  summaries.reserve(results.trials.size());
  observed_summaries.reserve(results.trials.size());
  std::cout << std::fixed;
  for (const auto& result : results.trials) {
    std::cout << "trial " << result.trial << ": " << result.elapsed_ns << " ns, ";
    if (benchmark == Benchmark::offered_load) {
      std::cout << "offered " << result.offered_messages.value_or(0) << ", observed "
                << result.observed_messages.value_or(0) << ", overwritten "
                << result.overwritten_messages.value_or(0) << ", retries "
                << result.retry_attempts.value_or(0) << ", observed payload bytes "
                << result.observed_payload_bytes.value_or(0);
      if (result.offered_messages_per_second) {
        std::cout << std::setprecision(0) << ", offered " << *result.offered_messages_per_second
                  << " messages/s";
        summaries.push_back(*result.offered_messages_per_second);
      } else {
        std::cout << ", offered rate unavailable";
      }
      if (result.observed_messages_per_second) {
        std::cout << std::setprecision(0) << ", observed " << *result.observed_messages_per_second
                  << " messages/s";
        observed_summaries.push_back(*result.observed_messages_per_second);
      } else {
        std::cout << ", observed rate unavailable";
      }
    } else if (benchmark == Benchmark::ping_pong) {
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

  if (benchmark == Benchmark::offered_load) {
    const auto median_offered_rate = median(std::move(summaries));
    const auto median_observed_rate = median(std::move(observed_summaries));
    std::cout << std::setprecision(0) << "median offered rate: ";
    if (median_offered_rate) {
      std::cout << *median_offered_rate << " messages/s\n";
    } else {
      std::cout << "unavailable\n";
    }
    std::cout << "median observed rate: ";
    if (median_observed_rate) {
      std::cout << *median_observed_rate << " messages/s\n";
    } else {
      std::cout << "unavailable\n";
    }
  } else if (benchmark == Benchmark::ping_pong) {
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
