#pragma once

#include "types.hpp"

namespace handoff::bench {

RunResults run_throughput(const Options& options);
RunResults run_fan_out_throughput(const Options& options);
RunResults run_pipeline_throughput(const Options& options);
RunResults run_ping_pong(const Options& options);
RunResults run_offered_load(const Options& options);

} // namespace handoff::bench
