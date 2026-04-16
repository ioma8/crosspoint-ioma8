# CrossPoint Reader: PDF Support + Bookmarks

Firmware for the Xteink X4 e-reader, forked from the original CrossPoint project. This build focuses on practical PDF
reading, SD-card bookmarks, EPUB/TXT reading, Wi-Fi upload, OTA updates, and KOReader Sync.

![](./docs/images/cover.jpg)

## Features

- PDF reader with text extraction, page rendering, outlines, cached loading, and stability fixes for larger documents
- SD-card bookmarks for EPUB, PDF, and TXT, stored next to each book cache
- EPUB and TXT reading with saved progress
- File browser, Wi-Fi book upload, OTA firmware updates, and KOReader Sync
- Configurable fonts, layout, rotation, refresh behavior, and display options

## Bookmarks

- Press `Down` + `Right` while reading to toggle a bookmark on the current page.
- Bookmarked pages show a small marker in the top-right corner.
- EPUB and PDF bookmarks are available from the reader menu under `Bookmarks`.
- TXT opens the bookmark list with the confirm button.
- Each bookmark stores the page position, approximate percentage, and the first words from that page.

## Installation

1. Download the latest `firmware.bin` from the [releases page](https://github.com/ioma8/crosspoint-ioma8/releases)
2. Connect your Xteink X4 to your computer via USB-C and wake/unlock the device
3. Open https://xteink.dve.al/ and flash the firmware file with the OTA flash controls

The release page provides a single firmware binary for the current build.

### Manual Build And Flash

Build and flash locally with PlatformIO:

```sh
pio run -e default --target upload
```

## Development Checks

```sh
bash test/pdf/run_pdf_parser_tests.sh
test/reader_bookmarks/run_reader_bookmark_tests.sh
pio run
```

PDF parser internals are documented in [PDF.md](./PDF.md).

---

CrossPoint Reader is not affiliated with Xteink or any manufacturer of the X4 hardware.
