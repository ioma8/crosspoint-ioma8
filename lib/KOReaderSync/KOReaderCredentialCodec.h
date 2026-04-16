#pragma once

#include <cstdint>
#include <string>

// Document matching method for KOReader sync
enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,  // Match by filename (simpler, works across different file sources)
  BINARY = 1,    // Match by partial MD5 of file content (more accurate, but files must be identical)
};

struct KOReaderCredentialData {
  std::string username;
  std::string password;
  std::string serverUrl;
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;
};

std::string encodeKOReaderCredentials(const KOReaderCredentialData& data);
bool decodeKOReaderCredentials(const char* json, KOReaderCredentialData& data, bool* needsResave = nullptr);
