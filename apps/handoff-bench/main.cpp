#include "handoff/platform/system_info.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
  std::uint64_t iterations{1'000'000};
  std::uint64_t warmup{10'000};
  unsigned int trials{3};
  std::optional<std::filesystem::path> output;
};

struct TrialResult {
  unsigned int trial;
  std::int64_t elapsed_ns;
  double operations_per_second;
  std::uint64_t checksum;
};

void print_usage(std::ostream& stream) {
  stream << "Usage:\n"
            "  handoff-bench help\n"
            "  handoff-bench list\n"
            "  handoff-bench run smoke [--iterations N] [--warmup N] [--trials N] "
            "[--output FILE]\n";
}

template <typename Integer> std::optional<Integer> parse_integer(std::string_view text) {
  Integer value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::optional<Options> parse_options(std::span<char*> arguments, std::ostream& errors) {
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
    } else {
      errors << "unknown option: " << argument << '\n';
      return std::nullopt;
    }
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

std::vector<TrialResult> run_smoke(const Options& options) {
  std::uint64_t checksum = smoke_work(options.warmup, 0);
  std::vector<TrialResult> results;
  results.reserve(options.trials);

  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    const auto start = Clock::now();
    checksum = smoke_work(options.iterations, checksum);
    const auto stop = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
    const double rate = static_cast<double>(options.iterations) * 1'000'000'000.0 /
                        static_cast<double>(std::max<std::int64_t>(elapsed, 1));
    results.push_back({.trial = trial,
                       .elapsed_ns = elapsed,
                       .operations_per_second = rate,
                       .checksum = checksum});
  }
  return results;
}

bool write_csv(const std::filesystem::path& path, std::uint64_t iterations,
               const std::vector<TrialResult>& results) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "unable to open output file: " << path << '\n';
    return false;
  }

  output << "benchmark,implementation,payload_bytes,capacity_slots,capacity_bytes,batch_size,"
            "iterations,trial,elapsed_ns,messages_per_second,latency_ns,checksum\n";
  for (const auto& result : results) {
    output << "smoke,harness,,,,," << iterations << ',' << result.trial << ',' << result.elapsed_ns
           << ",,," << result.checksum << '\n';
  }
  output.flush();
  if (!output) {
    std::cerr << "unable to write output file: " << path << '\n';
    return false;
  }
  return true;
}

int run_smoke_command(std::span<char*> arguments) {
  const auto options = parse_options(arguments, std::cerr);
  if (!options) {
    return 2;
  }

  const auto info = handoff::platform::current_system_info();
  const auto results = run_smoke(*options);
  std::cout << "smoke (harness plumbing only; not a handoff benchmark)\n"
            << "system: " << info.operating_system << ", " << info.architecture << ", "
            << info.compiler << '\n'
            << std::fixed << std::setprecision(0);
  for (const auto& result : results) {
    std::cout << "trial " << result.trial << ": " << result.elapsed_ns << " ns, "
              << result.operations_per_second << " operations/s, checksum " << result.checksum
              << '\n';
  }

  if (options->output && !write_csv(*options->output, options->iterations, results)) {
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
    std::cout << "smoke\tHarness timing and result-output plumbing check\n";
    return 0;
  }

  if (std::string_view(arguments.front()) == "run") {
    if (arguments.size() < 2) {
      std::cerr << "run requires a benchmark name\n";
      return 2;
    }
    if (std::string_view(arguments[1]) != "smoke") {
      std::cerr << "unknown benchmark: " << arguments[1] << '\n';
      return 2;
    }
    return run_smoke_command(arguments.subspan(2));
  }

  std::cerr << "unknown command: " << arguments.front() << '\n';
  print_usage(std::cerr);
  return 2;
}

int main(int argc, char* argv[]) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "fatal error: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "fatal error: unknown exception\n";
    return 1;
  }
}
