#pragma once

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ota_version {

struct Semver {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

inline bool hasRecognizedSuffix(const char* suffix) { return *suffix == '\0' || *suffix == '-' || *suffix == '+'; }

inline bool isDateTag(const char* version) {
  if (version == nullptr) {
    return false;
  }
  if (*version == 'v') {
    ++version;
  }
  // YYYY.MM.DD release tags use the semver parser shape but intentionally
  // include a descriptive suffix for each release built on that date.
  return std::strlen(version) >= 10 && std::isdigit(static_cast<unsigned char>(version[0])) &&
         std::isdigit(static_cast<unsigned char>(version[1])) &&
         std::isdigit(static_cast<unsigned char>(version[2])) &&
         std::isdigit(static_cast<unsigned char>(version[3])) && version[4] == '.' &&
         std::isdigit(static_cast<unsigned char>(version[5])) &&
         std::isdigit(static_cast<unsigned char>(version[6])) && version[7] == '.' &&
         std::isdigit(static_cast<unsigned char>(version[8])) &&
         std::isdigit(static_cast<unsigned char>(version[9]));
}

inline bool parseSemverPrefix(const char* version, Semver& out) {
  if (version == nullptr) {
    return false;
  }
  if (*version == 'v') {
    ++version;
  }

  char* end = nullptr;
  errno = 0;
  const long major = std::strtol(version, &end, 10);
  if (errno == ERANGE || major < 0 || major > INT_MAX || end == version || *end != '.') {
    return false;
  }
  version = end + 1;

  errno = 0;
  const long minor = std::strtol(version, &end, 10);
  if (errno == ERANGE || minor < 0 || minor > INT_MAX || end == version || *end != '.') {
    return false;
  }
  version = end + 1;

  errno = 0;
  const long patch = std::strtol(version, &end, 10);
  if (errno == ERANGE || patch < 0 || patch > INT_MAX || end == version || !hasRecognizedSuffix(end)) {
    return false;
  }

  out.major = static_cast<int>(major);
  out.minor = static_cast<int>(minor);
  out.patch = static_cast<int>(patch);
  return true;
}

inline bool isNewer(const std::string& latestVersion, const char* currentVersion) {
  if (latestVersion.empty() || currentVersion == nullptr || latestVersion == currentVersion) {
    return false;
  }

  Semver latest;
  Semver current;
  const bool latestParsed = parseSemverPrefix(latestVersion.c_str(), latest);
  const bool currentParsed = parseSemverPrefix(currentVersion, current);
  if (!latestParsed) {
    return false;
  }
  if (!currentParsed) {
    return true;
  }

  if (latest.major != current.major) return latest.major > current.major;
  if (latest.minor != current.minor) return latest.minor > current.minor;
  if (latest.patch != current.patch) return latest.patch > current.patch;

  if (std::strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  if (isDateTag(latestVersion.c_str()) && isDateTag(currentVersion)) {
    return latestVersion != currentVersion;
  }

  return false;
}

}  // namespace ota_version
