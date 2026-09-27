#include "command.hpp"

#include "output.hpp"
#include "rate.hpp"
#include "run_metadata.hpp"
#include "types.hpp"
#include "workloads.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

namespace handoff::bench {
namespace {

using Clock = std::chrono::steady_clock;

void print_usage(std::ostream& stream) {
  stream << "Usage:\n"
            "  handoff-bench help\n"
            "  handoff-bench list\n"
            "  handoff-bench run smoke [--iterations N] [--warmup N] [--trials N] "
            "[--output FILE]\n"
            "  handoff-bench run offered-load [--implementation sequence-payload] "
            "[--payload-bytes 8|64|256] [--capacity 64|1024]\n"
            "      [--producer-interval-ns 0..1000000] "
            "[--consumer-stall-every 0..1000000] "
            "[--consumer-stall-ns 0..1000000000]\n"
            "      [--iterations N] [--warmup N] [--trials N] [--producer-cpu N] "
            "[--consumer-cpu N] [--output FILE]\n"
            "  handoff-bench run publication-hole [--implementation "
            "mpsc-ordered|mpsc-count|mpsc-slot] "
            "[--payload-bytes 8|64|256] "
            "[--capacity 64|1024] [--output FILE]\n"
            "  handoff-bench run <throughput|ping-pong> "
            "[--implementation "
            "basic|batch|bulk|burst|byte-record|cache-line|cached-index|descriptor-record|fan-out|"
            "fixed-record|mpsc-count|mpsc-ordered|mpsc-serialized|mpsc-slot|pipeline|sequence|"
            "spmc-ordered|spmc-serialized|spmc-slot|staged] "
            "[--payload-bytes 8|64|256] [--capacity 64|1024] "
            "[--capacity-bytes 4096|65536] [--batch-size 1|4|16]\n"
            "      [--iterations N] [--warmup N] [--trials N] [--producer-cpu N] "
            "[--producer-cpus N,N] [--consumer-cpu N] [--consumer-cpus N,N] [--output FILE]\n";
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  Integer value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

bool is_known_option(std::string_view option) {
  return option == "--iterations" || option == "--warmup" || option == "--trials" ||
         option == "--output" || option == "--implementation" || option == "--payload-bytes" ||
         option == "--capacity" || option == "--capacity-bytes" || option == "--batch-size" ||
         option == "--producer-cpu" || option == "--producer-cpus" || option == "--consumer-cpu" ||
         option == "--consumer-cpus" || option == "--producer-interval-ns" ||
         option == "--consumer-stall-every" || option == "--consumer-stall-ns";
}

std::optional<std::array<unsigned int, 2>> parse_cpu_pair(std::string_view text) {
  const auto separator = text.find(',');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 == text.size() ||
      text.find(',', separator + 1) != std::string_view::npos) {
    return std::nullopt;
  }
  const auto first = parse_integer<unsigned int>(text.substr(0, separator));
  const auto second = parse_integer<unsigned int>(text.substr(separator + 1));
  if (!first || !second) {
    return std::nullopt;
  }
  return std::array{*first, *second};
}

bool applies_to_smoke(std::string_view option) {
  return option == "--iterations" || option == "--warmup" || option == "--trials" ||
         option == "--output";
}

std::optional<Options> parse_options(std::span<char*> arguments, Benchmark benchmark,
                                     std::ostream& errors) {
  Options options;
  if (benchmark == Benchmark::offered_load) {
    options.implementation = Implementation::sequence_payload;
  } else if (benchmark == Benchmark::publication_hole) {
    options.implementation = Implementation::mpsc_ordered;
  }
  bool slot_capacity_specified = false;
  bool byte_capacity_specified = false;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (!is_known_option(argument)) {
      errors << "unknown option: " << argument << '\n';
      return std::nullopt;
    }
    if (benchmark == Benchmark::smoke && !applies_to_smoke(argument)) {
      errors << "option " << argument << " does not apply to smoke\n";
      return std::nullopt;
    }
    if (benchmark == Benchmark::publication_hole && argument != "--implementation" &&
        argument != "--payload-bytes" && argument != "--capacity" && argument != "--output") {
      errors << "option " << argument << " does not apply to publication-hole\n";
      return std::nullopt;
    }
    if (benchmark == Benchmark::ping_pong && argument == "--batch-size") {
      errors << "option --batch-size does not apply to ping-pong\n";
      return std::nullopt;
    }
    if (benchmark == Benchmark::offered_load && argument == "--batch-size") {
      errors << "option --batch-size does not apply to offered-load\n";
      return std::nullopt;
    }
    if (benchmark != Benchmark::offered_load &&
        (argument == "--producer-interval-ns" || argument == "--consumer-stall-every" ||
         argument == "--consumer-stall-ns")) {
      errors << "option " << argument << " applies only to offered-load\n";
      return std::nullopt;
    }
    if (index + 1 >= arguments.size()) {
      errors << "missing value for " << argument << '\n';
      return std::nullopt;
    }
    const std::string_view value = arguments[++index];

    if (argument == "--iterations") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed || *parsed == 0) {
        errors << "--iterations must be a positive integer\n";
        return std::nullopt;
      }
      options.iterations = *parsed;
    } else if (argument == "--warmup") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed) {
        errors << "--warmup must be a non-negative integer\n";
        return std::nullopt;
      }
      options.warmup = *parsed;
    } else if (argument == "--trials") {
      const auto parsed = parse_integer<unsigned int>(value);
      if (!parsed || *parsed == 0) {
        errors << "--trials must be a positive integer\n";
        return std::nullopt;
      }
      options.trials = *parsed;
    } else if (argument == "--output") {
      if (value.empty()) {
        errors << "--output requires a file path\n";
        return std::nullopt;
      }
      options.output = std::filesystem::path(value);
    } else if (argument == "--implementation") {
      if (value == "basic") {
        options.implementation = Implementation::basic;
      } else if (value == "batch") {
        options.implementation = Implementation::batch;
      } else if (value == "bulk") {
        options.implementation = Implementation::bulk;
      } else if (value == "burst") {
        options.implementation = Implementation::burst;
      } else if (value == "byte-record") {
        options.implementation = Implementation::byte_record;
      } else if (value == "cache-line") {
        options.implementation = Implementation::cache_line;
      } else if (value == "cached-index") {
        options.implementation = Implementation::cached_index;
      } else if (value == "descriptor-record") {
        options.implementation = Implementation::descriptor_record;
      } else if (value == "fan-out") {
        options.implementation = Implementation::fan_out;
      } else if (value == "fixed-record") {
        options.implementation = Implementation::fixed_record;
      } else if (value == "mpsc-count") {
        options.implementation = Implementation::mpsc_count;
      } else if (value == "mpsc-ordered") {
        options.implementation = Implementation::mpsc_ordered;
      } else if (value == "mpsc-serialized") {
        options.implementation = Implementation::mpsc_serialized;
      } else if (value == "mpsc-slot") {
        options.implementation = Implementation::mpsc_slot;
      } else if (value == "pipeline") {
        options.implementation = Implementation::pipeline;
      } else if (value == "sequence") {
        options.implementation = Implementation::sequence;
      } else if (value == "sequence-payload") {
        options.implementation = Implementation::sequence_payload;
      } else if (value == "spmc-ordered") {
        options.implementation = Implementation::spmc_ordered;
      } else if (value == "spmc-serialized") {
        options.implementation = Implementation::spmc_serialized;
      } else if (value == "spmc-slot") {
        options.implementation = Implementation::spmc_slot;
      } else if (value == "staged") {
        options.implementation = Implementation::staged;
      } else {
        errors << "--implementation must be one of: basic, batch, bulk, burst, byte-record, "
                  "cache-line, cached-index, descriptor-record, fan-out, fixed-record, "
                  "mpsc-count, mpsc-ordered, mpsc-serialized, mpsc-slot, pipeline, sequence, "
                  "sequence-payload, spmc-ordered, spmc-serialized, spmc-slot, staged\n";
        return std::nullopt;
      }
    } else if (argument == "--payload-bytes") {
      const auto parsed = parse_integer<std::size_t>(value);
      if (!parsed || (*parsed != 8 && *parsed != 64 && *parsed != 256)) {
        errors << "--payload-bytes must be one of: 8, 64, 256\n";
        return std::nullopt;
      }
      options.payload_bytes = *parsed;
    } else if (argument == "--capacity") {
      const auto parsed = parse_integer<std::size_t>(value);
      if (!parsed || (*parsed != 64 && *parsed != 1'024)) {
        errors << "--capacity must be one of: 64, 1024\n";
        return std::nullopt;
      }
      options.capacity_slots = *parsed;
      slot_capacity_specified = true;
    } else if (argument == "--capacity-bytes") {
      const auto parsed = parse_integer<std::size_t>(value);
      if (!parsed || (*parsed != 4'096 && *parsed != 65'536)) {
        errors << "--capacity-bytes must be one of: 4096, 65536\n";
        return std::nullopt;
      }
      options.capacity_bytes = parsed;
      byte_capacity_specified = true;
    } else if (argument == "--batch-size") {
      const auto parsed = parse_integer<std::size_t>(value);
      if (!parsed || (*parsed != 1 && *parsed != 4 && *parsed != 16)) {
        errors << "--batch-size must be one of: 1, 4, 16\n";
        return std::nullopt;
      }
      options.batch_size = *parsed;
    } else if (argument == "--producer-cpu") {
      const auto parsed = parse_integer<unsigned int>(value);
      if (!parsed) {
        errors << "--producer-cpu must be a non-negative integer\n";
        return std::nullopt;
      }
      options.producer_cpu = parsed;
    } else if (argument == "--producer-cpus") {
      const auto parsed = parse_cpu_pair(value);
      if (!parsed) {
        errors << "--producer-cpus must contain exactly two non-negative integers separated by a "
                  "comma\n";
        return std::nullopt;
      }
      options.producer_cpus = parsed;
    } else if (argument == "--consumer-cpu") {
      const auto parsed = parse_integer<unsigned int>(value);
      if (!parsed) {
        errors << "--consumer-cpu must be a non-negative integer\n";
        return std::nullopt;
      }
      options.consumer_cpu = parsed;
    } else if (argument == "--consumer-cpus") {
      const auto parsed = parse_cpu_pair(value);
      if (!parsed) {
        errors << "--consumer-cpus must contain exactly two non-negative integers separated by a "
                  "comma\n";
        return std::nullopt;
      }
      options.consumer_cpus = parsed;
    } else if (argument == "--producer-interval-ns") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed || *parsed > 1'000'000) {
        errors << "--producer-interval-ns must be in the range 0..1000000\n";
        return std::nullopt;
      }
      options.producer_interval_ns = *parsed;
    } else if (argument == "--consumer-stall-every") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed || *parsed > 1'000'000) {
        errors << "--consumer-stall-every must be in the range 0..1000000\n";
        return std::nullopt;
      }
      options.consumer_stall_every = *parsed;
    } else if (argument == "--consumer-stall-ns") {
      const auto parsed = parse_integer<std::uint64_t>(value);
      if (!parsed || *parsed > 1'000'000'000) {
        errors << "--consumer-stall-ns must be in the range 0..1000000000\n";
        return std::nullopt;
      }
      options.consumer_stall_ns = *parsed;
    }
  }

  if (benchmark == Benchmark::offered_load &&
      options.implementation != Implementation::sequence_payload) {
    errors << "offered-load requires implementation sequence-payload\n";
    return std::nullopt;
  }
  if (benchmark != Benchmark::offered_load &&
      options.implementation == Implementation::sequence_payload) {
    errors << "implementation sequence-payload applies only to offered-load\n";
    return std::nullopt;
  }
  if ((options.consumer_stall_every == 0) != (options.consumer_stall_ns == 0)) {
    errors << "--consumer-stall-every and --consumer-stall-ns must both be zero or both be "
              "positive\n";
    return std::nullopt;
  }
  if (options.implementation == Implementation::byte_record) {
    if (slot_capacity_specified) {
      errors << "--capacity does not apply to byte-record; use --capacity-bytes\n";
      return std::nullopt;
    }
    if (!options.capacity_bytes) {
      options.capacity_bytes = 65'536;
    }
  } else if (options.implementation == Implementation::descriptor_record) {
    if (slot_capacity_specified != byte_capacity_specified) {
      errors << "descriptor-record requires --capacity and --capacity-bytes together\n";
      return std::nullopt;
    }
    if (!options.capacity_bytes) {
      options.capacity_bytes = 65'536;
    }
    const bool small = options.capacity_slots == 64 && *options.capacity_bytes == 4'096;
    const bool large = options.capacity_slots == 1'024 && *options.capacity_bytes == 65'536;
    if (!small && !large) {
      errors << "descriptor-record capacity pairs must be 64/4096 or 1024/65536\n";
      return std::nullopt;
    }
  } else if (options.capacity_bytes) {
    errors << "--capacity-bytes requires implementation byte-record or descriptor-record\n";
    return std::nullopt;
  }

  const bool multi_producer = options.implementation == Implementation::mpsc_count ||
                              options.implementation == Implementation::mpsc_ordered ||
                              options.implementation == Implementation::mpsc_serialized ||
                              options.implementation == Implementation::mpsc_slot;
  const bool spmc = options.implementation == Implementation::spmc_ordered ||
                    options.implementation == Implementation::spmc_serialized ||
                    options.implementation == Implementation::spmc_slot;
  const bool multi_consumer = options.implementation == Implementation::fan_out ||
                              options.implementation == Implementation::pipeline || spmc;
  if (spmc && benchmark != Benchmark::throughput) {
    errors << "SPMC implementations apply only to throughput\n";
    return std::nullopt;
  }
  if (multi_producer) {
    if (benchmark != Benchmark::throughput && benchmark != Benchmark::publication_hole) {
      errors << "MPSC implementations apply only to throughput\n";
      return std::nullopt;
    }
    if (options.producer_cpu || options.consumer_cpus) {
      errors << "MPSC placement uses --producer-cpus and --consumer-cpu\n";
      return std::nullopt;
    }
    if (options.producer_cpus.has_value() != options.consumer_cpu.has_value()) {
      errors << "MPSC placement requires --producer-cpus and --consumer-cpu together\n";
      return std::nullopt;
    }
    if (options.producer_cpus) {
      const auto [first, second] = *options.producer_cpus;
      if (first == second || first == *options.consumer_cpu || second == *options.consumer_cpu) {
        errors << "MPSC producer and consumer CPUs must be distinct\n";
        return std::nullopt;
      }
    }
  } else if (multi_consumer) {
    if (options.producer_cpus) {
      errors << "--producer-cpus applies only to MPSC implementations\n";
      return std::nullopt;
    }
    if (options.consumer_cpu) {
      errors << "--consumer-cpu does not apply to multi-consumer implementations; use "
                "--consumer-cpus\n";
      return std::nullopt;
    }
    if (options.producer_cpu.has_value() != options.consumer_cpus.has_value()) {
      errors << "multi-consumer placement requires --producer-cpu and --consumer-cpus together\n";
      return std::nullopt;
    }
    if (options.consumer_cpus) {
      const auto [first, second] = *options.consumer_cpus;
      if (first == second) {
        errors << "multi-consumer CPUs must be distinct\n";
        return std::nullopt;
      }
      if (*options.producer_cpu == first || *options.producer_cpu == second) {
        errors << "producer and consumer CPUs must be distinct\n";
        return std::nullopt;
      }
    }
  } else {
    if (options.producer_cpus) {
      errors << "--producer-cpus applies only to MPSC implementations\n";
      return std::nullopt;
    }
    if (options.consumer_cpus) {
      errors << "--consumer-cpus applies only to multi-consumer implementations\n";
      return std::nullopt;
    }
    if (options.producer_cpu && options.consumer_cpu &&
        *options.producer_cpu == *options.consumer_cpu) {
      errors << "producer and consumer CPUs must be different\n";
      return std::nullopt;
    }
  }
  if (benchmark == Benchmark::publication_hole &&
      options.implementation != Implementation::mpsc_ordered &&
      options.implementation != Implementation::mpsc_count &&
      options.implementation != Implementation::mpsc_slot) {
    errors << "publication-hole requires mpsc-ordered, mpsc-count, or mpsc-slot\n";
    return std::nullopt;
  }
  if (benchmark == Benchmark::throughput && options.batch_size > 1 &&
      options.implementation != Implementation::basic &&
      options.implementation != Implementation::batch &&
      options.implementation != Implementation::bulk &&
      options.implementation != Implementation::burst &&
      options.implementation != Implementation::staged) {
    errors << "--batch-size greater than 1 requires implementation basic, batch, bulk, burst, or "
              "staged\n";
    return std::nullopt;
  }
  if (benchmark == Benchmark::ping_pong && (options.implementation == Implementation::bulk ||
                                            options.implementation == Implementation::burst ||
                                            options.implementation == Implementation::fan_out ||
                                            options.implementation == Implementation::pipeline ||
                                            options.implementation == Implementation::staged)) {
    errors << "implementations bulk, burst, fan-out, pipeline, and staged apply only to "
              "throughput\n";
    return std::nullopt;
  }
  if (benchmark == Benchmark::throughput &&
      (options.iterations % options.batch_size != 0 || options.warmup % options.batch_size != 0)) {
    errors << "--iterations and --warmup must be divisible by --batch-size\n";
    return std::nullopt;
  }
  if ((options.implementation == Implementation::sequence ||
       options.implementation == Implementation::fan_out ||
       options.implementation == Implementation::pipeline || multi_producer || spmc) &&
      options.warmup > std::numeric_limits<std::uint64_t>::max() - options.iterations) {
    errors << "--iterations plus --warmup exceeds the sequence range\n";
    return std::nullopt;
  }
  constexpr auto payload_sequence_limit = std::numeric_limits<std::uint64_t>::max() - 1;
  if (options.implementation == Implementation::sequence_payload &&
      (options.iterations > payload_sequence_limit ||
       options.warmup > payload_sequence_limit - options.iterations)) {
    errors << "--iterations plus --warmup exceeds the sequence-payload range\n";
    return std::nullopt;
  }
  return options;
}

std::uint64_t smoke_work(std::uint64_t iterations, std::uint64_t seed) {
  std::uint64_t checksum = seed;
  for (std::uint64_t index = 0; index < iterations; ++index) {
    checksum ^= index + 0x9e3779b97f4a7c15ULL + (checksum << 6U) + (checksum >> 2U);
  }
  return checksum;
}

RunResults run_smoke(const Options& options) {
  std::uint64_t checksum = smoke_work(options.warmup, 0);
  RunResults results;
  results.trials.reserve(options.trials);

  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    const auto start = Clock::now();
    checksum = smoke_work(options.iterations, checksum);
    const auto stop = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    const auto rate = rate_per_second(options.iterations, elapsed);
    results.trials.push_back({.trial = trial,
                              .elapsed_ns = elapsed,
                              .messages_per_second = rate,
                              .latency_ns = std::nullopt,
                              .latency_p95_ns = std::nullopt,
                              .latency_p99_ns = std::nullopt,
                              .checksum = checksum});
  }
  return results;
}

int run_benchmark_command(Benchmark benchmark, std::span<char*> arguments) {
  const auto options = parse_options(arguments, benchmark, std::cerr);
  if (!options) {
    return 2;
  }

  const auto metadata = collect_run_metadata();
  RunResults results;
  switch (benchmark) {
  case Benchmark::smoke:
    results = run_smoke(*options);
    break;
  case Benchmark::throughput:
    results = run_throughput(*options);
    break;
  case Benchmark::ping_pong:
    results = run_ping_pong(*options);
    break;
  case Benchmark::offered_load:
    results = run_offered_load(*options);
    break;
  case Benchmark::publication_hole:
    results = run_publication_hole(*options);
    break;
  }
  print_results(benchmark, *options, results, metadata);
  if (options->output && !write_csv(*options->output, benchmark, *options, results, metadata)) {
    return 1;
  }
  return 0;
}

} // namespace

int run(int argc, char* argv[]) {
  const std::span<char*> arguments(argv + 1, static_cast<std::size_t>(argc - 1));
  if (arguments.empty() || std::string_view(arguments.front()) == "help" ||
      std::string_view(arguments.front()) == "--help") {
    print_usage(std::cout);
    return 0;
  }

  if (std::string_view(arguments.front()) == "list") {
    std::cout << "smoke\tHarness timing and result-output plumbing check\n"
                 "throughput\tSteady-state completed handoffs\n"
                 "ping-pong\tSPSC round-trip latency (RTT; RTT/2 is a proxy)\n"
                 "offered-load\tLossy offered and observed sequence-payload publications\n"
                 "publication-hole\tBounded MPSC publication progress diagnostic\n";
    return 0;
  }

  if (std::string_view(arguments.front()) == "run") {
    if (arguments.size() < 2) {
      std::cerr << "run requires a benchmark name\n";
      return 2;
    }
    const std::string_view name = arguments[1];
    if (name == "smoke") {
      return run_benchmark_command(Benchmark::smoke, arguments.subspan(2));
    }
    if (name == "throughput") {
      return run_benchmark_command(Benchmark::throughput, arguments.subspan(2));
    }
    if (name == "ping-pong") {
      return run_benchmark_command(Benchmark::ping_pong, arguments.subspan(2));
    }
    if (name == "offered-load") {
      return run_benchmark_command(Benchmark::offered_load, arguments.subspan(2));
    }
    if (name == "publication-hole") {
      return run_benchmark_command(Benchmark::publication_hole, arguments.subspan(2));
    }
    std::cerr << "unknown benchmark: " << name << '\n';
    return 2;
  }

  std::cerr << "unknown command: " << arguments.front() << '\n';
  print_usage(std::cerr);
  return 2;
}

} // namespace handoff::bench
