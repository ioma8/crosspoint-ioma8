#include "app/WifiCredentialStore.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>

#include "app/persistence/JsonSettingsIO.h"

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
// File paths
constexpr char WIFI_FILE_BIN[] = "/.crosspoint/wifi.bin";
constexpr char WIFI_FILE_JSON[] = "/.crosspoint/wifi.json";
}  // namespace

bool WifiCredentialStore::saveToFile() const {
  FsHelpers::ensureCrossPointDataDir();
  return JsonSettingsIO::saveWifi(*this, WIFI_FILE_JSON);
}

bool WifiCredentialStore::loadFromFile() {
  // Try JSON first
  if (Storage.exists(WIFI_FILE_JSON)) {
    String json = Storage.readFile(WIFI_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = JsonSettingsIO::loadWifi(*this, json.c_str(), &resave);
      if (result && resave) {
        LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
        if (!saveToFile()) {
          LOG_ERR("WCS", "Failed to resave WiFi credentials after format update");
        }
      }
      return result;
    }
  }

  // Fall back to binary migration
  if (Storage.exists(WIFI_FILE_BIN)) {
    LOG_ERR("WCS", "Legacy wifi.bin credentials are no longer migrated");
  }

  return false;
}

bool WifiCredentialStore::loadFromBinaryFile() {
  return false;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  // Check if this SSID already exists and update it
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    if (cred->password == password) {
      LOG_DBG("WCS", "Credentials unchanged for: %s", ssid.c_str());
      return true;
    }
    cred->password = password;
    LOG_DBG("WCS", "Updated credentials for: %s", ssid.c_str());
    return saveToFile();
  }

  // Check if we've reached the limit
  if (credentials.size() >= MAX_NETWORKS) {
    LOG_DBG("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
    return false;
  }

  // Add new credential
  credentials.push_back({ssid, password});
  LOG_DBG("WCS", "Added credentials for: %s", ssid.c_str());
  return saveToFile();
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    credentials.erase(cred);
    LOG_DBG("WCS", "Removed credentials for: %s", ssid.c_str());
    if (ssid == lastConnectedSsid) {
      clearLastConnectedSsid();
    }
    return saveToFile();
  }
  return false;  // Not found
}

const WifiCredential* WifiCredentialStore::findCredential(const std::string& ssid) const {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return &*cred;
  }

  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid) != nullptr; }

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  if (lastConnectedSsid != ssid) {
    lastConnectedSsid = ssid;
    if (!saveToFile()) {
      LOG_ERR("WCS", "Failed to save last connected SSID");
    }
  }
}

const std::string& WifiCredentialStore::getLastConnectedSsid() const { return lastConnectedSsid; }

void WifiCredentialStore::clearLastConnectedSsid() {
  if (!lastConnectedSsid.empty()) {
    lastConnectedSsid.clear();
    if (!saveToFile()) {
      LOG_ERR("WCS", "Failed to clear last connected SSID on disk");
    }
  }
}

void WifiCredentialStore::clearAll() {
  credentials.clear();
  lastConnectedSsid.clear();
  if (!saveToFile()) {
    LOG_ERR("WCS", "Failed to clear WiFi credentials on disk");
  }
  LOG_DBG("WCS", "Cleared all WiFi credentials");
}
