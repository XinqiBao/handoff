#pragma once

#include <cstdint>
#include <type_traits>

namespace handoff::record {

struct RecordHeader {
  std::uint64_t sequence{};
  std::uint32_t type_tag{};
  std::uint32_t payload_length{};
};

static_assert(std::is_standard_layout_v<RecordHeader>);
static_assert(sizeof(RecordHeader) == 16);

} // namespace handoff::record
