#pragma once

#include <string>

namespace obfuscation {

std::string obfuscateToBase64(const std::string& plaintext);
std::string deobfuscateFromBase64(const char* encoded, bool* ok = nullptr);

}  // namespace obfuscation
