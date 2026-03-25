#pragma once

#include <cstddef>
#include <cstdint>

// Minimal Arduino Print for host PDF parser tests.
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) { return 1; }
};
