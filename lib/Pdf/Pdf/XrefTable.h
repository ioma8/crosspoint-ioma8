#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class XrefTable {
  std::vector<uint32_t> offsets;  // file byte offset; 0 = free or in object stream only
  uint32_t rootObjId_ = 0;
  std::unordered_map<uint32_t, std::string> inlineDict_;
  std::unordered_map<uint32_t, std::vector<uint8_t>> inlineStream_;
  std::unordered_set<uint32_t> loadedObjStreams_;

  bool parseXrefStream(FsFile& file, size_t fileSize, uint32_t xrefObjOffset);
  void loadObjStream(FsFile& file, uint32_t stmObjId);

 public:
  bool parse(FsFile& file);
  uint32_t getOffset(uint32_t objId) const;
  uint32_t objectCount() const;
  uint32_t rootObjId() const;

  // Resolve object dict (and optional stream) from file offset or object stream cache.
  bool readDictForObject(FsFile& file, uint32_t objId, std::string& dictBody) const;
  bool readStreamForObject(FsFile& file, uint32_t objId, std::string& dictOut, std::vector<uint8_t>& streamPayload,
                           bool& flateDecode) const;
};
