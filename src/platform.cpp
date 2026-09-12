#include "handoff/platform/system_info.hpp"
#include "handoff/platform/thread_affinity.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace handoff::platform {
namespace {

std::string operating_system() {
#if defined(__APPLE__)
  return "macOS";
#elif defined(__linux__)
  return "Linux";
#else
  return "unknown";
#endif
}

std::string architecture() {
#if defined(__aarch64__) || defined(__arm64__)
  return "arm64";
#elif defined(__x86_64__)
  return "x86_64";
#else
  return "unknown";
#endif
}

std::string trim(std::string value) {
  const auto is_space = [](unsigned char character) { return std::isspace(character) != 0; };
  value.erase(value.begin(), std::ranges::find_if_not(value, is_space));
  value.erase(std::ranges::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
  return value;
}

std::string cpu_model() {
#if defined(__APPLE__)
  std::size_t size = 0;
  if (sysctlbyname("machdep.cpu.brand_string", nullptr, &size, nullptr, 0) != 0 || size == 0) {
    return "unknown";
  }
  std::string model(size, '\0');
  if (sysctlbyname("machdep.cpu.brand_string", model.data(), &size, nullptr, 0) != 0) {
    return "unknown";
  }
  if (!model.empty() && model.back() == '\0') {
    model.pop_back();
  }
  return trim(std::move(model));
#elif defined(__linux__)
  std::ifstream cpu_info("/proc/cpuinfo");
  std::string line;
  while (std::getline(cpu_info, line)) {
    const auto separator = line.find(':');
    if (separator == std::string::npos) {
      continue;
    }
    const auto key = trim(line.substr(0, separator));
    if (key == "model name" || key == "Hardware" || key == "Processor") {
      const auto model = trim(line.substr(separator + 1));
      if (!model.empty()) {
        return model;
      }
    }
  }
  return "unknown";
#else
  return "unknown";
#endif
}

} // namespace

SystemInfo current_system_info() {
  return {.operating_system = operating_system(),
          .architecture = architecture(),
          .compiler = "Clang",
          .compiler_version = __clang_version__,
          .cpu_model = cpu_model()};
}

AffinityResult pin_current_thread(unsigned int cpu) {
#if defined(__linux__)
  if (cpu >= static_cast<unsigned int>(CPU_SETSIZE)) {
    return {.status = AffinityStatus::invalid_cpu, .message = "CPU index exceeds CPU_SETSIZE"};
  }

  const auto cpu_index = static_cast<int>(cpu);
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu_index, &set);
  const int error = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  if (error != 0) {
    return {.status = AffinityStatus::system_error,
            .message = std::system_category().message(error)};
  }
  return {.status = AffinityStatus::applied, .message = "thread affinity applied"};
#else
  static_cast<void>(cpu);
  return {.status = AffinityStatus::unsupported,
          .message = "thread affinity is unsupported on this platform"};
#endif
}

} // namespace handoff::platform
