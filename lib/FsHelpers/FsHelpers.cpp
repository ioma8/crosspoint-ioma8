#include "FsHelpers.h"

#include <HalStorage.h>

#include <cctype>
#include <cstring>
#include <vector>

namespace FsHelpers {

namespace {
constexpr char CROSSPOINT_DATA_DIR[] = "/.crosspoint";
}

std::string normalisePath(const std::string& path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else if (component != ".") {
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    if (component == "..") {
      if (!components.empty()) {
        components.pop_back();
      }
    } else if (component != ".") {
      components.push_back(component);
    }
  }

  std::string result;
  for (const auto& c : components) {
    if (!result.empty()) {
      result += "/";
    }
    result += c;
  }

  return result;
}

std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string dirName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

std::string stem(const std::string& path) {
  const std::string name = baseName(path);
  const size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0) {
    return name;
  }
  return name.substr(0, dot);
}

bool ensureDirectory(const std::string& path) { return Storage.exists(path.c_str()) || Storage.mkdir(path.c_str()); }

bool ensureDirectoryRecursive(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  if (Storage.exists(path.c_str())) {
    return true;
  }

  for (size_t i = 1; i < path.length(); i++) {
    if (path[i] == '/') {
      const std::string parent = path.substr(0, i);
      if (!parent.empty() && !ensureDirectory(parent)) {
        return false;
      }
    }
  }
  return ensureDirectory(path);
}

bool ensureCrossPointDataDir() { return ensureDirectory(CROSSPOINT_DATA_DIR); }

bool checkFileExtension(std::string_view fileName, const char* extension) {
  const size_t extLen = strlen(extension);
  if (fileName.length() < extLen) {
    return false;
  }

  const size_t offset = fileName.length() - extLen;
  for (size_t i = 0; i < extLen; i++) {
    if (tolower(static_cast<unsigned char>(fileName[offset + i])) !=
        tolower(static_cast<unsigned char>(extension[i]))) {
      return false;
    }
  }
  return true;
}

bool hasJpgExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".jpg") || checkFileExtension(fileName, ".jpeg");
}

bool hasPngExtension(std::string_view fileName) { return checkFileExtension(fileName, ".png"); }

bool hasBmpExtension(std::string_view fileName) { return checkFileExtension(fileName, ".bmp"); }

bool hasGifExtension(std::string_view fileName) { return checkFileExtension(fileName, ".gif"); }

bool hasEpubExtension(std::string_view fileName) { return checkFileExtension(fileName, ".epub"); }

bool hasXtcExtension(std::string_view fileName) {
  return checkFileExtension(fileName, ".xtc") || checkFileExtension(fileName, ".xtch");
}

bool hasTxtExtension(std::string_view fileName) { return checkFileExtension(fileName, ".txt"); }

bool hasMarkdownExtension(std::string_view fileName) { return checkFileExtension(fileName, ".md"); }

bool hasPdfExtension(std::string_view fileName) { return checkFileExtension(fileName, ".pdf"); }

FileType detectFileType(std::string_view path) {
  if (!path.empty() && path.back() == '/') {
    return FileType::Directory;
  }
  if (hasEpubExtension(path)) {
    return FileType::Epub;
  }
  if (hasPdfExtension(path)) {
    return FileType::Pdf;
  }
  if (hasXtcExtension(path)) {
    return FileType::Xtc;
  }
  if (isTextDocument(path)) {
    return FileType::Text;
  }
  if (hasBmpExtension(path)) {
    return FileType::Image;
  }
  return FileType::Other;
}

bool isTextDocument(std::string_view path) { return hasTxtExtension(path) || hasMarkdownExtension(path); }

bool isReadableDocument(std::string_view path) {
  const FileType type = detectFileType(path);
  return type == FileType::Epub || type == FileType::Pdf || type == FileType::Xtc || type == FileType::Text;
}

bool isSupportedFile(std::string_view path) {
  const FileType type = detectFileType(path);
  return type != FileType::Other && type != FileType::Directory;
}

}  // namespace FsHelpers
