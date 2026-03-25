#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct PdfTextBlock {
  std::string text;
  uint32_t orderHint = 0;  // Y position from PDF (descending = top)
};

struct PdfImageDescriptor {
  uint32_t pdfStreamOffset = 0;
  uint32_t pdfStreamLength = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t format = 0;  // 0=JPEG, 1=PNG/FlateDecode
};

struct PdfDrawStep {
  bool isImage = false;
  uint32_t index = 0;
};

struct PdfPage {
  std::vector<PdfTextBlock> textBlocks;
  std::vector<PdfImageDescriptor> images;
  std::vector<PdfDrawStep> drawOrder;
};
