#pragma once

#include "workload_support.hpp"

#include "handoff/record/fixed_record_ring.hpp"

#include <cstddef>
#include <cstdint>

namespace handoff::bench {

inline constexpr std::uint32_t benchmark_record_type = 1;

template <std::size_t Bytes> record::FixedRecord<Bytes> make_fixed_record(std::uint64_t sequence) {
  const auto payload = make_payload<Bytes>(sequence);
  return {.header = {.sequence = sequence,
                     .type_tag = benchmark_record_type,
                     .payload_length = static_cast<std::uint32_t>(Bytes)},
          .payload = payload.bytes};
}

template <std::size_t Bytes>
bool observe_fixed_record(const record::FixedRecord<Bytes>& record, std::uint64_t expected_sequence,
                          std::uint64_t& checksum) {
  const bool header_valid = record.header.sequence == expected_sequence &&
                            record.header.type_tag == benchmark_record_type &&
                            record.header.payload_length == Bytes;
  return observe_payload_bytes(record.payload, expected_sequence, checksum) && header_valid;
}

} // namespace handoff::bench
