#include "KOReaderCredentialCodec.h"

#include <cassert>
#include <iostream>
#include <string>

namespace obfuscation {

std::string obfuscateToBase64(const std::string& plaintext) { return "obf:" + plaintext; }

std::string deobfuscateFromBase64(const char* encoded, bool* ok) {
  const std::string value = encoded ? encoded : "";
  constexpr const char* prefix = "obf:";
  if (value.rfind(prefix, 0) != 0) {
    if (ok) *ok = false;
    return "";
  }
  if (ok) *ok = true;
  return value.substr(std::string(prefix).size());
}

}  // namespace obfuscation

int main() {
  KOReaderCredentialData data;
  bool needsResave = false;

  assert(decodeKOReaderCredentials(
      R"({"username":"alice","password_obf":"obf:s3cret","serverUrl":"sync.local","matchMethod":1})", data,
      &needsResave));
  assert(data.username == "alice");
  assert(data.password == "s3cret");
  assert(data.serverUrl == "sync.local");
  assert(data.matchMethod == DocumentMatchMethod::BINARY);
  assert(!needsResave);

  assert(decodeKOReaderCredentials(R"({"username":"bob","password":"legacy","serverUrl":"","matchMethod":0})", data,
                                   &needsResave));
  assert(data.username == "bob");
  assert(data.password == "legacy");
  assert(data.serverUrl.empty());
  assert(data.matchMethod == DocumentMatchMethod::FILENAME);
  assert(needsResave);

  const std::string json = encodeKOReaderCredentials(data);
  assert(json.find("\"username\":\"bob\"") != std::string::npos);
  assert(json.find("\"password_obf\":\"obf:legacy\"") != std::string::npos);
  assert(json.find("\"password\"") == std::string::npos);

  std::cout << "KOReader credential codec tests passed\n";
  return 0;
}
