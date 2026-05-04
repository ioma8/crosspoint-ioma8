#pragma once
#include <cstdint>
#include <string>

#include "KOReaderCredentialCodec.h"

/**
 * Singleton class for storing KOReader sync credentials on the SD card.
 * Passwords are XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON (not cryptographically secure,
 * but prevents casual reading and ties credentials to the specific device).
 */
class KOReaderCredentialStore {
 private:
  static KOReaderCredentialStore instance;
  std::string username;
  std::string password;
  std::string serverUrl;                                            // Custom sync server URL (empty = default)
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;  // Default to filename for compatibility
  bool loaded = false;

  // Private constructor for singleton
  KOReaderCredentialStore() = default;

  bool loadFromBinaryFile();
  bool loadFromJson(const char* json, bool* needsResave);

 public:
  // Delete copy constructor and assignment
  KOReaderCredentialStore(const KOReaderCredentialStore&) = delete;
  KOReaderCredentialStore& operator=(const KOReaderCredentialStore&) = delete;

  // Get singleton instance
  static KOReaderCredentialStore& getInstance() { return instance; }

  // Save/load from SD card
  bool saveToFile() const;
  bool loadFromFile();
  bool ensureLoaded();

  // Credential management
  void setCredentials(const std::string& user, const std::string& pass);
  const std::string& getUsername();
  const std::string& getPassword();

  // Get MD5 hash of password for API authentication
  std::string getMd5Password();

  // Check if credentials are set
  bool hasCredentials();

  // Clear credentials
  void clearCredentials();

  // Server URL management
  void setServerUrl(const std::string& url);
  const std::string& getServerUrl();

  // Get base URL for API calls (with http:// normalization if no protocol, falls back to default)
  std::string getBaseUrl();

  // Document matching method
  void setMatchMethod(DocumentMatchMethod method);
  DocumentMatchMethod getMatchMethod();
};

// Helper macro to access credential store
#define KOREADER_STORE KOReaderCredentialStore::getInstance()
