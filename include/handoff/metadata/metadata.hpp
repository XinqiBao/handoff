#pragma once

#include <cstdint>
#include <type_traits>

namespace handoff::metadata {

struct Metadata {
  std::uint64_t signature{};
  std::uint64_t chunk{};
  std::uint32_t length{};
  std::uint32_t control{};

  friend constexpr bool operator==(const Metadata&, const Metadata&) = default;
};

static_assert(std::is_standard_layout_v<Metadata>);

} // namespace handoff::metadata
