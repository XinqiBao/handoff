#pragma once

#include <optional>
#include <stdexcept>
#include <utility>

template <typename Token> Token required_spmc_token(std::optional<Token> token) {
  if (!token) {
    throw std::runtime_error("required SPMC token was unavailable");
  }
  return std::move(*token);
}
