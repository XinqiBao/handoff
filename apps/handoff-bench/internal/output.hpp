#pragma once

#include "types.hpp"

#include <filesystem>

namespace handoff::bench {

bool write_csv(const std::filesystem::path& path, Benchmark benchmark, const Options& options,
               const RunResults& results);
void print_results(Benchmark benchmark, const Options& options, const RunResults& results);

} // namespace handoff::bench
