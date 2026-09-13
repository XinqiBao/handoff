#pragma once

#include "workload_support.hpp"

#include "handoff/descriptor/descriptor_payload_ring.hpp"
#include "handoff/record/fixed_record_ring.hpp"
#include "handoff/record/variable_record_ring.hpp"

#include <cstddef>
#include <cstdint>

namespace handoff::bench {

inline constexpr std::uint32_t benchmark_record_type = 1;

template <std::size_t Bytes> record::RecordHeader make_record_header(std::uint64_t sequence) {
  return {.sequence = sequence,
          .type_tag = benchmark_record_type,
          .payload_length = static_cast<std::uint32_t>(Bytes)};
}

template <std::size_t Bytes>
bool observe_record_header(const record::RecordHeader& header, std::uint64_t expected_sequence) {
  return header.sequence == expected_sequence && header.type_tag == benchmark_record_type &&
         header.payload_length == Bytes;
}

template <std::size_t Bytes> record::FixedRecord<Bytes> make_fixed_record(std::uint64_t sequence) {
  const auto payload = make_payload<Bytes>(sequence);
  return {.header = make_record_header<Bytes>(sequence), .payload = payload.bytes};
}

template <std::size_t Bytes>
bool observe_fixed_record(const record::FixedRecord<Bytes>& value, std::uint64_t expected_sequence,
                          std::uint64_t& checksum) {
  return observe_payload_bytes(value.payload, expected_sequence, checksum) &&
         observe_record_header<Bytes>(value.header, expected_sequence);
}

template <std::size_t Bytes> struct RecordMessage {
  record::RecordHeader header{};
  Payload<Bytes> payload{};
};

template <std::size_t Bytes> RecordMessage<Bytes> make_record_message(std::uint64_t sequence) {
  return {.header = make_record_header<Bytes>(sequence), .payload = make_payload<Bytes>(sequence)};
}

template <std::size_t Bytes>
bool observe_record_message(const RecordMessage<Bytes>& value, std::uint64_t expected_sequence,
                            std::uint64_t& checksum) {
  return observe_payload(value.payload, expected_sequence, checksum) &&
         observe_record_header<Bytes>(value.header, expected_sequence);
}

} // namespace handoff::bench
