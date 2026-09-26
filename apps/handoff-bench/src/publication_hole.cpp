#include "workload_support.hpp"
#include "workloads.hpp"

#include "handoff/mpsc/ordered_publication_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace handoff::bench {
namespace {

template <std::size_t Bytes, std::size_t Capacity> RunResults run_probe() {
  mpsc::OrderedPublicationRing<Payload<Bytes>, Capacity> ring;
  std::latch first_claimed{1};
  std::latch later_ready{1};
  std::latch close_hole{1};
  std::atomic<bool> first_failed{false};
  std::atomic<std::size_t> later_publication_returns{0};
  std::size_t later_claims = 0;
  std::size_t rejected_publications = 0;

  std::thread first([&] {
    auto claim = ring.try_claim();
    if (!claim) {
      first_failed.store(true, std::memory_order_relaxed);
      first_claimed.count_down();
      return;
    }
    auto token = std::move(*claim);
    first_claimed.count_down();
    close_hole.wait();
    token.value() = make_payload<Bytes>(token.position());
    token.publish();
  });
  first_claimed.wait();
  if (first_failed.load(std::memory_order_relaxed)) {
    first.join();
    throw std::runtime_error("publication-hole first claim failed");
  }

  std::thread later([&] {
    using Ring = mpsc::OrderedPublicationRing<Payload<Bytes>, Capacity>;
    std::vector<typename Ring::ProducerClaim> claims;
    std::vector<bool> published_early;
    claims.reserve(Capacity - 1);
    published_early.reserve(Capacity - 1);
    for (std::size_t index = 1; index < Capacity; ++index) {
      auto claim = ring.try_claim();
      if (!claim) {
        break;
      }
      auto token = std::move(*claim);
      token.value() = make_payload<Bytes>(token.position());
      claims.push_back(std::move(token));
    }
    later_claims = claims.size();
    for (auto& claim : claims) {
      const bool published = claim.try_publish();
      published_early.push_back(published);
      if (!published) {
        ++rejected_publications;
      }
    }
    later_ready.count_down();
    for (std::size_t index = 0; index < claims.size(); ++index) {
      if (!published_early[index]) {
        claims[index].publish();
        later_publication_returns.fetch_add(1, std::memory_order_release);
      }
    }
  });

  later_ready.wait();
  auto extra_claim = ring.try_claim();
  const bool further_claim_rejected = !extra_claim;
  auto visible = ring.try_observe();
  const auto visible_before_release = static_cast<std::size_t>(visible.has_value());
  if (visible) {
    visible->cancel();
  }
  const auto publication_returns_before_release =
      later_publication_returns.load(std::memory_order_acquire);
  close_hole.count_down();
  first.join();
  later.join();
  if (extra_claim) {
    extra_claim->value() = make_payload<Bytes>(extra_claim->position());
    extra_claim->publish();
  }

  std::uint64_t checksum = 0;
  std::size_t completions = 0;
  bool valid = true;
  for (std::size_t position = 0; position < Capacity; ++position) {
    auto observation = ring.try_observe();
    if (!observation) {
      valid = false;
      break;
    }
    valid = valid && observation->position() == position &&
            observe_payload(observation->value(), position, checksum);
    observation->release();
    ++completions;
  }
  if (!valid || later_claims != Capacity - 1 || rejected_publications != Capacity - 1 ||
      !further_claim_rejected || visible_before_release != 0 ||
      publication_returns_before_release != 0 || completions != Capacity ||
      checksum != expected_checksum<Bytes>(Capacity)) {
    throw std::runtime_error("publication-hole progress validation failed");
  }

  RunResults results;
  results.progress = {.claimed_before_release = Capacity,
                      .payloads_completed_before_release = Capacity - 1,
                      .publication_attempts_rejected_before_release = rejected_publications,
                      .publication_returns_before_release = publication_returns_before_release,
                      .visible_before_release = visible_before_release,
                      .consumer_completions_before_release = 0,
                      .further_claim_rejected = further_claim_rejected,
                      .final_consumer_completions = completions,
                      .checksum = checksum};
  return results;
}

template <std::size_t Bytes> RunResults dispatch_capacity(const Options& options) {
  switch (options.capacity_slots) {
  case 64:
    return run_probe<Bytes, 64>();
  case 1'024:
    return run_probe<Bytes, 1'024>();
  default:
    throw std::logic_error("validated publication-hole capacity was not dispatched");
  }
}

} // namespace

RunResults run_publication_hole(const Options& options) {
  switch (options.payload_bytes) {
  case 8:
    return dispatch_capacity<8>(options);
  case 64:
    return dispatch_capacity<64>(options);
  case 256:
    return dispatch_capacity<256>(options);
  default:
    throw std::logic_error("validated publication-hole payload size was not dispatched");
  }
}

} // namespace handoff::bench
