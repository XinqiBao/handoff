#include "handoff/platform/system_info.hpp"
#include "handoff/platform/thread_affinity.hpp"
#include "handoff/spsc/basic_bounded_ring.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class Benchmark { smoke, throughput, ping_pong };

struct Options {
  std::uint64_t iterations{1'000'000};
  std::uint64_t warmup{10'000};
  unsigned int trials{3};
  std::string implementation{"basic"};
  std::size_t payload_bytes{64};
  std::size_t capacity_slots{1'024};
  std::optional<unsigned int> producer_cpu;
  std::optional<unsigned int> consumer_cpu;
  std::optional<std::filesystem::path> output;
};

struct TrialResult {
  unsigned int trial;
  std::int64_t elapsed_ns;
  double messages_per_second;
  double latency_ns;
  double latency_p95_ns;
  double latency_p99_ns;
  std::uint64_t checksum;
};

struct PlacementResult {
  std::optional<unsigned int> requested;
  handoff::platform::AffinityResult outcome;
};

struct RunResults {
  std::vector<TrialResult> trials;
  PlacementResult producer_placement;
  PlacementResult consumer_placement;
};

struct TrialControl {
  std::atomic<unsigned int> ready{0};
  std::atomic<unsigned int> warmed{0};
  std::atomic<bool> begin_warmup{false};
  std::atomic<bool> begin_timed{false};
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
  std::atomic<bool> valid{true};
};

void print_usage(std::ostream& stream) {
  stream << "Usage:\n"
            "  handoff-bench help\n"
            "  handoff-bench list\n"
            "  handoff-bench run smoke [--iterations N] [--warmup N] [--trials N] "
            "[--output FILE]\n"
            "  handoff-bench run <throughput|ping-pong> [--implementation basic] "
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
      options.implementation = value;
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

  if (benchmark != Benchmark::smoke && options.implementation != "basic") {
    errors << "--implementation must be: basic\n";
    return std::nullopt;
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

template <std::size_t Bytes> struct Payload {
  static_assert(Bytes >= sizeof(std::uint64_t));
  std::array<std::byte, Bytes> bytes{};
};

template <std::size_t Bytes> Payload<Bytes> make_payload(std::uint64_t sequence) {
  static_assert(sizeof(Payload<Bytes>) == Bytes);
  Payload<Bytes> payload;
  for (std::size_t index = 0; index < sizeof(sequence); ++index) {
    payload.bytes[index] = static_cast<std::byte>((sequence >> (index * 8U)) & 0xffU);
  }
  for (std::size_t index = sizeof(sequence); index < payload.bytes.size(); ++index) {
    payload.bytes[index] = static_cast<std::byte>((sequence + index * 17U) & 0xffU);
  }
  return payload;
}

template <std::size_t Bytes>
bool observe_payload(const Payload<Bytes>& payload, std::uint64_t expected_sequence,
                     std::uint64_t& checksum) {
  std::uint64_t observed_sequence = 0;
  for (std::size_t index = 0; index < sizeof(observed_sequence); ++index) {
    observed_sequence |=
        static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(payload.bytes[index]))
        << (index * 8U);
  }

  bool valid = observed_sequence == expected_sequence;
  checksum ^= observed_sequence + 0x9e3779b97f4a7c15ULL + (checksum << 6U) + (checksum >> 2U);
  for (std::size_t index = 0; index < payload.bytes.size(); ++index) {
    const auto expected = index < sizeof(expected_sequence)
                              ? static_cast<std::byte>((expected_sequence >> (index * 8U)) & 0xffU)
                              : static_cast<std::byte>((expected_sequence + index * 17U) & 0xffU);
    valid = valid && payload.bytes[index] == expected;
    checksum += std::to_integer<std::uint8_t>(payload.bytes[index]);
  }
  return valid;
}

template <std::size_t Bytes> std::uint64_t expected_checksum(std::uint64_t iterations) {
  std::uint64_t checksum = 0;
  for (std::uint64_t sequence = 0; sequence < iterations; ++sequence) {
    const auto payload = make_payload<Bytes>(sequence);
    static_cast<void>(observe_payload(payload, sequence, checksum));
  }
  return checksum;
}

struct LatencySummary {
  double median_ns;
  double p95_ns;
  double p99_ns;
};

LatencySummary summarize_latency(std::vector<std::int64_t> samples) {
  std::ranges::sort(samples);
  const auto middle = samples.size() / 2;
  const double median_ns =
      samples.size() % 2 == 1
          ? static_cast<double>(samples[middle])
          : (static_cast<double>(samples[middle - 1]) + static_cast<double>(samples[middle])) / 2.0;
  const auto p95_index = (samples.size() - 1) * 95 / 100;
  const auto p99_index = (samples.size() - 1) * 99 / 100;
  return {.median_ns = median_ns,
          .p95_ns = static_cast<double>(samples[p95_index]),
          .p99_ns = static_cast<double>(samples[p99_index])};
}

PlacementResult apply_affinity(std::optional<unsigned int> cpu) {
  if (!cpu) {
    return {.requested = std::nullopt,
            .outcome = {.status = handoff::platform::AffinityStatus::unsupported,
                        .message = "not requested"}};
  }
  return {.requested = cpu, .outcome = handoff::platform::pin_current_thread(*cpu)};
}

bool affinity_failed(const PlacementResult& placement) {
  if (!placement.requested) {
    return false;
  }
  return placement.outcome.status == handoff::platform::AffinityStatus::invalid_cpu ||
         placement.outcome.status == handoff::platform::AffinityStatus::system_error;
}

void wait_for_count(const std::atomic<unsigned int>& count, unsigned int expected) {
  while (count.load(std::memory_order_acquire) != expected) {
    std::this_thread::yield();
  }
}

bool wait_for_phase(const std::atomic<bool>& phase, const std::atomic<bool>& cancel) {
  while (!phase.load(std::memory_order_acquire)) {
    if (cancel.load(std::memory_order_acquire)) {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

void validate_affinity_or_cancel(TrialControl& control, std::thread& producer,
                                 std::thread& consumer, const PlacementResult& producer_placement,
                                 const PlacementResult& consumer_placement) {
  const bool producer_failed = affinity_failed(producer_placement);
  const bool consumer_failed = affinity_failed(consumer_placement);
  if (!producer_failed && !consumer_failed) {
    return;
  }

  control.cancel.store(true, std::memory_order_release);
  producer.join();
  consumer.join();
  const auto& failed = producer_failed ? producer_placement : consumer_placement;
  const std::string role = producer_failed ? "producer" : "consumer";
  throw std::runtime_error(role + " affinity failed: " + failed.outcome.message);
}

template <std::size_t Bytes, std::size_t Capacity>
TrialResult run_throughput_trial(const Options& options, unsigned int trial,
                                 PlacementResult& producer_placement,
                                 PlacementResult& consumer_placement) {
  using Ring = handoff::spsc::BasicBoundedRing<Payload<Bytes>, Capacity>;
  Ring ring;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      auto payload = make_payload<Bytes>(sequence);
      while (!ring.try_push(payload)) {
        std::this_thread::yield();
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      auto payload = make_payload<Bytes>(sequence);
      while (!ring.try_push(payload)) {
        std::this_thread::yield();
      }
    }
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    Payload<Bytes> payload;
    std::uint64_t warmup_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      while (!ring.try_pop(payload)) {
        std::this_thread::yield();
      }
      if (!observe_payload(payload, sequence, warmup_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      while (!ring.try_pop(payload)) {
        std::this_thread::yield();
      }
      if (!observe_payload(payload, sequence, checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    stop = Clock::now();
    control.done.store(true, std::memory_order_release);
  });

  wait_for_count(control.ready, 2);
  validate_affinity_or_cancel(control, producer, consumer, producer_placement, consumer_placement);
  control.begin_warmup.store(true, std::memory_order_release);
  wait_for_count(control.warmed, 2);
  const auto start = Clock::now();
  control.begin_timed.store(true, std::memory_order_release);
  while (!control.done.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  producer.join();
  consumer.join();

  if (!control.valid.load(std::memory_order_relaxed) || checksum != expected) {
    throw std::runtime_error("throughput payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  const auto safe_elapsed = std::max<std::int64_t>(elapsed, 1);
  const double rate =
      static_cast<double>(options.iterations) * 1'000'000'000.0 / static_cast<double>(safe_elapsed);
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = rate,
          .latency_ns = 0.0,
          .latency_p95_ns = 0.0,
          .latency_p99_ns = 0.0,
          .checksum = checksum};
}

template <std::size_t Bytes, std::size_t Capacity>
TrialResult run_ping_pong_trial(const Options& options, unsigned int trial,
                                PlacementResult& producer_placement,
                                PlacementResult& consumer_placement) {
  using Ring = handoff::spsc::BasicBoundedRing<Payload<Bytes>, Capacity>;
  Ring requests;
  Ring responses;
  TrialControl control;
  Clock::time_point stop;
  std::uint64_t checksum = 0;
  const auto expected = expected_checksum<Bytes>(options.iterations);
  std::vector<std::int64_t> rtt_samples(static_cast<std::size_t>(options.iterations));

  std::thread producer([&] {
    producer_placement = apply_affinity(options.producer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    Payload<Bytes> response;
    std::uint64_t warmup_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      auto request = make_payload<Bytes>(sequence);
      while (!requests.try_push(request)) {
        std::this_thread::yield();
      }
      while (!responses.try_pop(response)) {
        std::this_thread::yield();
      }
      if (!observe_payload(response, sequence, warmup_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      auto request = make_payload<Bytes>(sequence);
      const auto sample_start = Clock::now();
      while (!requests.try_push(request)) {
        std::this_thread::yield();
      }
      while (!responses.try_pop(response)) {
        std::this_thread::yield();
      }
      const auto sample_stop = Clock::now();
      rtt_samples[static_cast<std::size_t>(sequence)] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(sample_stop - sample_start).count();
      if (!observe_payload(response, sequence, checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
    }
    stop = Clock::now();
    control.done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    consumer_placement = apply_affinity(options.consumer_cpu);
    control.ready.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_warmup, control.cancel)) {
      return;
    }

    Payload<Bytes> request;
    std::uint64_t ignored_checksum = 0;
    for (std::uint64_t sequence = 0; sequence < options.warmup; ++sequence) {
      while (!requests.try_pop(request)) {
        std::this_thread::yield();
      }
      if (!observe_payload(request, sequence, ignored_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
      while (!responses.try_push(request)) {
        std::this_thread::yield();
      }
    }
    control.warmed.fetch_add(1, std::memory_order_release);
    if (!wait_for_phase(control.begin_timed, control.cancel)) {
      return;
    }

    for (std::uint64_t sequence = 0; sequence < options.iterations; ++sequence) {
      while (!requests.try_pop(request)) {
        std::this_thread::yield();
      }
      if (!observe_payload(request, sequence, ignored_checksum)) {
        control.valid.store(false, std::memory_order_relaxed);
      }
      while (!responses.try_push(request)) {
        std::this_thread::yield();
      }
    }
  });

  wait_for_count(control.ready, 2);
  validate_affinity_or_cancel(control, producer, consumer, producer_placement, consumer_placement);
  control.begin_warmup.store(true, std::memory_order_release);
  wait_for_count(control.warmed, 2);
  const auto start = Clock::now();
  control.begin_timed.store(true, std::memory_order_release);
  while (!control.done.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  producer.join();
  consumer.join();

  if (!control.valid.load(std::memory_order_relaxed) || checksum != expected) {
    throw std::runtime_error("ping-pong payload validation failed");
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count();
  const auto latency = summarize_latency(std::move(rtt_samples));
  return {.trial = trial,
          .elapsed_ns = elapsed,
          .messages_per_second = 0.0,
          .latency_ns = latency.median_ns,
          .latency_p95_ns = latency.p95_ns,
          .latency_p99_ns = latency.p99_ns,
          .checksum = checksum};
}

template <std::size_t Bytes, std::size_t Capacity>
RunResults run_spsc(const Options& options, Benchmark benchmark) {
  RunResults results;
  results.trials.reserve(options.trials);
  for (unsigned int trial = 1; trial <= options.trials; ++trial) {
    PlacementResult producer_placement;
    PlacementResult consumer_placement;
    TrialResult result = benchmark == Benchmark::throughput
                             ? run_throughput_trial<Bytes, Capacity>(
                                   options, trial, producer_placement, consumer_placement)
                             : run_ping_pong_trial<Bytes, Capacity>(
                                   options, trial, producer_placement, consumer_placement);
    if (trial == 1) {
      results.producer_placement = std::move(producer_placement);
      results.consumer_placement = std::move(consumer_placement);
    }
    results.trials.push_back(result);
  }
  return results;
}

template <std::size_t Bytes>
RunResults dispatch_capacity(const Options& options, Benchmark benchmark) {
  switch (options.capacity_slots) {
  case 64:
    return run_spsc<Bytes, 64>(options, benchmark);
  case 1'024:
    return run_spsc<Bytes, 1'024>(options, benchmark);
  default:
    throw std::logic_error("validated capacity was not dispatched");
  }
}

RunResults run_spsc_benchmark(const Options& options, Benchmark benchmark) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<8>(options, benchmark);
  case 64:
    return dispatch_capacity<64>(options, benchmark);
  case 256:
    return dispatch_capacity<256>(options, benchmark);
  default:
    throw std::logic_error("validated payload size was not dispatched");
  }
}

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

std::string placement_value(const PlacementResult& placement) {
  if (!placement.requested) {
    return "not-requested";
  }
  if (placement.outcome.status == handoff::platform::AffinityStatus::applied) {
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

bool write_csv(const std::filesystem::path& path, Benchmark benchmark, const Options& options,
               const RunResults& results) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "unable to open output file: " << path << '\n';
    return false;
  }

  const auto info = handoff::platform::current_system_info();
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
      output << benchmark_name(benchmark) << ',' << options.implementation << ','
             << options.payload_bytes << ',' << options.capacity_slots << ','
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
  const auto info = handoff::platform::current_system_info();
  std::cout << benchmark_name(benchmark);
  if (benchmark == Benchmark::smoke) {
    std::cout << " (harness plumbing only; not a handoff benchmark)";
  } else {
    std::cout << " / " << options.implementation << " / " << options.payload_bytes << " B / "
              << options.capacity_slots << " slots";
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

int run_benchmark_command(Benchmark benchmark, std::span<char*> arguments) {
  const auto options = parse_options(arguments, benchmark, std::cerr);
  if (!options) {
    return 2;
  }

  const auto results =
      benchmark == Benchmark::smoke ? run_smoke(*options) : run_spsc_benchmark(*options, benchmark);
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
