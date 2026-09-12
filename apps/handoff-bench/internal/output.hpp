#pragma once

#include "run_metadata.hpp"
#include "types.hpp"

#include <filesystem>

namespace handoff::bench {

bool write_csv(const std::filesystem::path& path, Benchmark benchmark, const Options& options,
               const RunResults& results, const RunMetadata& metadata);
void print_results(Benchmark benchmark, const Options& options, const RunResults& results,
                   const RunMetadata& metadata);

} // namespace handoff::bench
