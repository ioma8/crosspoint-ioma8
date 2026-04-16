#include "KOReaderCredentialCodec.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

std::string encodeKOReaderCredentials(const KOReaderCredentialData& data) {
  JsonDocument doc;
  doc["username"] = data.username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(data.password);
  doc["serverUrl"] = data.serverUrl;
  doc["matchMethod"] = static_cast<uint8_t>(data.matchMethod);

  std::string json;
  serializeJson(doc, json);
  return json;
}

bool decodeKOReaderCredentials(const char* json, KOReaderCredentialData& data, bool* needsResave) {
  if (needsResave) *needsResave = false;

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    return false;
  }

  data.username = doc["username"] | std::string("");
  bool ok = false;
  data.password = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok || data.password.empty()) {
    data.password = doc["password"] | std::string("");
    if (!data.password.empty() && needsResave) *needsResave = true;
  }
  data.serverUrl = doc["serverUrl"] | std::string("");
  const uint8_t method = doc["matchMethod"] | static_cast<uint8_t>(DocumentMatchMethod::FILENAME);
  data.matchMethod = static_cast<DocumentMatchMethod>(method);
  return true;
}
