#pragma once

#include "types.hpp"

namespace handoff::bench {

RunResults run_throughput(const Options& options);
RunResults run_ping_pong(const Options& options);

} // namespace handoff::bench
