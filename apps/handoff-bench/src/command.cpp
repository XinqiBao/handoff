#include "command.hpp"

#include "output.hpp"
#include "types.hpp"
#include "workloads.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
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
            "  handoff-bench run <throughput|ping-pong> "
            "[--implementation basic|cache-line] "
            "[--payload-bytes 8|64|256] [--capacity 64|1024]\n"
            "      [--iterations N] [--warmup N] [--trials N] [--producer-cpu N] "
            "[--consumer-cpu N] [--output FILE]\n";
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  Integer value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<Options> parse_options(std::span<char*> arguments, Benchmark benchmark,
                                     std::ostream& errors) {
  Options options;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
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
    } else if (benchmark == Benchmark::smoke) {
      errors << "option " << argument << " does not apply to smoke\n";
      return std::nullopt;
    } else if (argument == "--implementation") {
      if (value == "basic") {
        options.implementation = Implementation::basic;
      } else if (value == "cache-line") {
        options.implementation = Implementation::cache_line;
      } else {
        errors << "--implementation must be one of: basic, cache-line\n";
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
    } else if (argument == "--producer-cpu") {
      const auto parsed = parse_integer<unsigned int>(value);
      if (!parsed) {
        errors << "--producer-cpu must be a non-negative integer\n";
        return std::nullopt;
      }
      options.producer_cpu = parsed;
    } else if (argument == "--consumer-cpu") {
      const auto parsed = parse_integer<unsigned int>(value);
      if (!parsed) {
        errors << "--consumer-cpu must be a non-negative integer\n";
        return std::nullopt;
      }
      options.consumer_cpu = parsed;
    } else {
      errors << "unknown option: " << argument << '\n';
      return std::nullopt;
    }
  }

  if (options.producer_cpu && options.consumer_cpu &&
      *options.producer_cpu == *options.consumer_cpu) {
    errors << "producer and consumer CPUs must be different\n";
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
    const double rate = static_cast<double>(options.iterations) * 1'000'000'000.0 /
                        static_cast<double>(std::max<std::int64_t>(elapsed, 1));
    results.trials.push_back({.trial = trial,
                              .elapsed_ns = elapsed,
                              .messages_per_second = rate,
                              .latency_ns = 0.0,
                              .latency_p95_ns = 0.0,
                              .latency_p99_ns = 0.0,
                              .checksum = checksum});
  }
  return results;
}

int run_benchmark_command(Benchmark benchmark, std::span<char*> arguments) {
  const auto options = parse_options(arguments, benchmark, std::cerr);
  if (!options) {
    return 2;
  }

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
  }
  print_results(benchmark, *options, results);
  if (options->output && !write_csv(*options->output, benchmark, *options, results)) {
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
                 "throughput\tSteady-state completed SPSC handoffs\n"
                 "ping-pong\tSPSC round-trip latency (RTT; RTT/2 is a proxy)\n";
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
    std::cerr << "unknown benchmark: " << name << '\n';
    return 2;
  }

  std::cerr << "unknown command: " << arguments.front() << '\n';
  print_usage(std::cerr);
  return 2;
}

} // namespace handoff::bench
