# PDF Crash Debug Notes

## Current Status

The latest device crash is still a stack protection fault, but it no longer points at the PDF
object parser stack buffers.

Decoded against the current local firmware ELF:

```text
0x4201af92: tinf_decode_trees at lib/uzlib/src/tinflate.c:308
0x4201b2b2: uzlib_uncompress at lib/uzlib/src/tinflate.c:590
0x4201b5be: InflateReader::readAtMost at lib/InflateReader/InflateReader.cpp:100
0x4205e04e: StreamDecoder::flateDecodeBytes at lib/Pdf/Pdf/StreamDecoder.cpp:149
```

The reported task stack was already below its lower bound:

```text
Stack pointer: 0x3fcb3ba0
Stack bounds:  0x3fcb3c70 - 0x3fcb5c60
```

So the crash is consistent with inflate/decompression stack pressure during PDF stream decoding.

## Important Log Noise

These messages appear before the crash but are not currently the best root-cause candidate:

```text
Failed to open file for reading: /.crosspoint/.../meta.bin
Failed to open file for reading: /.crosspoint/.../pages/1.bin
Failed to open file for reading: /.crosspoint/.../bookmarks.txt
```

Those happen on cold cache / missing cache files and are expected to be recoverable.

The image size messages are also separate:

```text
Image too large (1665x1981 = 3298365 pixels JPEG), max supported: 3145728 pixels
```

That needs graceful rendering behavior, but it does not explain the decoded stack frame inside
`uzlib`.

## Likely Root Cause

`InflateReader` currently stores `uzlib_uncomp` directly as a member. Local `InflateReader` variables
therefore place the full uzlib state on the task stack, including two Huffman trees:

```cpp
TINF_TREE ltree;
TINF_TREE dtree;
```

`tinf_decode_trees()` then adds another local table:

```c
unsigned char lengths[288 + 32];
```

On the ESP32-C3 `ActivityManager` task this is enough to overflow when the PDF path is already deep
in stream decoding.

## Applied Fix

Move the `uzlib_uncomp` state owned by `InflateReader` to scoped heap storage:

```cpp
std::unique_ptr<uzlib_uncomp> decomp;
```

Allocate it in `InflateReader::init()` with `new (std::nothrow)` and release it in `deinit()` /
the destructor. This keeps the memory lifetime short and RAII-managed while removing the large
uzlib state from caller stack frames.

Because some streaming callbacks currently depend on object layout by casting `uzlib_uncomp*` back
to a larger context, add an explicit context pointer to `uzlib_uncomp`:

```c
void* user_context;
```

Then update callbacks to use it:

```cpp
auto* ctx = static_cast<MyContext*>(uncomp->user_context);
```

Affected callback users found so far:

- `lib/ZipFile/ZipFile.cpp`
- `lib/PngToBmpConverter/PngToBmpConverter.cpp`
- `test/pdf/pdf_parser_host_test.cpp`
- `lib/Pdf/Pdf/StreamDecoder.cpp` already uses a global file-inflate context for file streams,
  but can still use `raw()` after `init()`.

Keep `tinf_decode_trees()` unchanged initially. Its `lengths[320]` stack table is much smaller than
the current `InflateReader` stack payload, and changing the vendor C inflate code is a broader risk.

## Included Stack-Scratch Direction

The same fix set includes changes moving remaining PDF object-body scratch buffers from stack to
scoped heap in:

- `lib/Pdf/Pdf.cpp`
- `lib/Pdf/Pdf/ContentStream.cpp`
- `test/pdf/pdf_parser_host_test.cpp`
- `hypotheses.tsv`

The final local verification passed:

```text
./test/pdf/run_pdf_parser_tests.sh
git diff --check
./bin/clang-format-fix -g
pio run
```
