#pragma once

#include <cstdint>
#include <string>

namespace PageProgressStore {

bool save(const char* logTag, const std::string& cachePath, uint32_t page);
bool load(const char* logTag, const std::string& cachePath, uint32_t& page);

}  // namespace PageProgressStore
