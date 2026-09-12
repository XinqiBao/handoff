#include "command.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
  try {
    return handoff::bench::run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "fatal error: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "fatal error: unknown exception\n";
    return 1;
  }
}
