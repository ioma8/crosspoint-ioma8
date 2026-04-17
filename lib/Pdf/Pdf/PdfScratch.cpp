#include "PdfScratch.h"

#include <cstdlib>
#include <new>

namespace PdfScratch {
namespace {

ToUnicodeWorkspace* g_toUnicodeWorkspace = nullptr;
bool g_toUnicodeWorkspaceInUse = false;

void freeWorkspaceIfIdle() {
  if (g_toUnicodeWorkspaceInUse || !g_toUnicodeWorkspace) {
    return;
  }
  g_toUnicodeWorkspace->~ToUnicodeWorkspace();
  std::free(g_toUnicodeWorkspace);
  g_toUnicodeWorkspace = nullptr;
}

}  // namespace

ToUnicodeWorkspace* acquireToUnicodeWorkspace() {
  if (g_toUnicodeWorkspaceInUse) {
    return nullptr;
  }
  if (!g_toUnicodeWorkspace) {
    void* mem = std::malloc(sizeof(ToUnicodeWorkspace));
    if (!mem) {
      return nullptr;
    }
    g_toUnicodeWorkspace = new (mem) ToUnicodeWorkspace();
  }
  g_toUnicodeWorkspaceInUse = true;
  return g_toUnicodeWorkspace;
}

void releaseToUnicodeWorkspaceUse() {
  g_toUnicodeWorkspaceInUse = false;
  freeWorkspaceIfIdle();
}

void releaseRetainedBuffers() { freeWorkspaceIfIdle(); }

size_t retainedBufferBytes() { return g_toUnicodeWorkspace ? sizeof(ToUnicodeWorkspace) : 0; }

}  // namespace PdfScratch
