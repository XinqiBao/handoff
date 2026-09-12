#include "handoff/platform/system_info.hpp"
#include "handoff/platform/thread_affinity.hpp"

#include <string>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>

#include <cstring>
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

std::string compiler() { return "Clang " __clang_version__; }

} // namespace

SystemInfo current_system_info() {
  return {.operating_system = operating_system(),
          .architecture = architecture(),
          .compiler = compiler()};
}

AffinityResult pin_current_thread(unsigned int cpu) {
#if defined(__linux__)
  if (cpu >= CPU_SETSIZE) {
    return {.status = AffinityStatus::invalid_cpu, .message = "CPU index exceeds CPU_SETSIZE"};
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  const int error = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  if (error != 0) {
    return {.status = AffinityStatus::system_error, .message = std::strerror(error)};
  }
  return {.status = AffinityStatus::applied, .message = "thread affinity applied"};
#else
  static_cast<void>(cpu);
  return {.status = AffinityStatus::unsupported,
          .message = "thread affinity is unsupported on this platform"};
#endif
}

} // namespace handoff::platform
