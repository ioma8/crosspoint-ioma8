# Audit Fixes

- NET-C1: Added HTTP Basic Auth checks to all CrossPoint web routes, WebDAV handlers, and WebSocket uploads. Credentials can be overridden at build time with `CROSSPOINT_WEB_AUTH_USER` and `CROSSPOINT_WEB_AUTH_PASSWORD`.
- NET-C2: Normalized download paths and replaced last-segment-only protection with full-path protected-segment checks.
- NET-C3: Rejected WebSocket uploads targeting protected paths or unsafe filenames before opening destination files.
- NET-H1: Normalized file-list paths and denied listings under protected path segments such as `/.crosspoint`.
- NET-H2: Rejected HTTP multipart upload filenames containing path separators, `..`, or protected item names.
- NET-H3: Rejected WebSocket upload filenames containing path separators, `..`, or protected item names.
- NET-H4: Normalized delete paths and replaced last-segment-only protection with full-path protected-segment checks.
- NET-H5: Reduced `handleDownload` stack transfer buffer from 4096 bytes to 256 bytes.
- NET-H6: Reduced `WebDAVHandler::handleCopy` stack transfer buffer from 4096 bytes to 256 bytes.
- NET-H7: Added null check for `new WebSocketsServer(...)` before use.
- NET-M6: Replaced WebSocket upload size parsing via `String::toInt()` with checked `strtoull` parsing and `size_t` bounds validation.
- NET-M7: Normalized create-folder parent paths and rejected creation under protected segments.
- NET-L2: Removed partial HTTP upload files when final buffered write fails.
- NET-L6: Added null check for `new WebDAVHandler()` before registration.
- SER-C3: Added a 64 KiB maximum cache string length and short-read clearing for `serialization::readString`.
- ZIP-C1: Replaced unaligned EOCD `reinterpret_cast` reads with `memcpy` into typed locals.
- ZIP-C2: Added oversized/overflow guards before `readFileToMemory` allocation.
- ZIP-H1: Freed the output buffer when the secondary deflated-data allocation fails.
- ZIP-H2: Initialized central-directory length fields and checked key `file.read()` return values before use.
- ZIP-M1: Added checked central-directory reads in the file-stat scan paths touched by this fix pass.
- ZIP-L1: Corrected a ZIP read error log format for `uint32_t` and `size_t` values.
- HAL-C1: Replaced raw panic stack-pointer word dereferences with `memcpy` to avoid RISC-V alignment faults.
- HAL-M1: Marked `__wrap_abort` with `IRAM_ATTR`.
- NET-C4: Removed unconditional `NetworkClientSecure::setInsecure()` calls from HTTP downloader HTTPS paths and attached the ESP-IDF CA bundle for certificate validation.
- NET-H8: Checked `HTTPClient::writeToStream()` return value in the stream fetch path.
- NET-C5: Re-enabled OTA TLS hostname validation by clearing `skip_cert_common_name_check` for release metadata and firmware downloads.
- KOR-C1: Removed unconditional `WiFiClientSecure::setInsecure()` from KOReader sync HTTPS calls and attached the ESP-IDF CA bundle for certificate validation.
- KOR-C2: Stopped sending HTTP Basic Auth plaintext passwords on non-HTTPS KOReader sync URLs.
- KOR-H1: Added a 16 KiB bounded stream reader before parsing KOReader progress JSON, including chunked responses without `Content-Length`.
- ACT-H1: Added a `FINISHED` state transition to `SHUTTING_DOWN` so successful OTA installs restart automatically.
- OPDS-M1: Added a null parser guard in `OpdsParser::flush()`.
- OPDS-M2: Capped OPDS text-node accumulation and entry count to prevent unbounded heap growth.
- NET-M8: Stopped treating `v1.2.3` and `1.2.3` as different/newer when semver numbers match.
- NET-L1: Added `strtol` overflow and range checks in OTA semver parsing.
- MAP-M1: Clamped `sideButtonLayout` before indexing `kSideLayouts`.
- MAP-M2: Clamped remapped front-button hardware indexes before passing them to `InputManager`.
- ACT-C3: Moved the 500-byte filename buffer in `FileBrowserActivity::loadFiles()` from task stack to static storage.
- ACT-C4: Moved the 500-byte filename buffer in `SleepActivity::renderCustomSleepScreen()` from task stack to static storage.

- ACT-C1: Added cancellation tracking for EPUB home thumbnail generation so EpubReaderActivity::onExit() deletes any outstanding FreeRTOS thumbnail task and releases its context before dropping EPUB state.

- ACT-C2: Added cancellation tracking for XTC home thumbnail generation so XtcReaderActivity::onExit() deletes any outstanding FreeRTOS thumbnail task and releases its context before dropping XTC state.

- EPUB-H3: Added Section::loadPageFromSectionFile() bounds checks for currentPage, truncated section files, LUT offsets, and page offsets before seeking into cached section data.

- EPUB-H4: Added Page::deserialize() guards for excessive page element counts and failed text/image block deserialization so corrupt caches cannot drive unbounded element allocation or null page elements.

- EPUB-H5: Added a bounded FsFile string reader and used it for TextBlock cached words so corrupt word lengths are rejected before oversized allocation.

- EPUB-H6: Closed tempNavFile on TOC nav parser setup and allocation failures so parseTocNavFile() no longer leaks SD file handles on those error paths.

- GFX-H1: Made ditherer error-row allocations non-throwing, added validity checks, and guarded ditherer methods against null row buffers after allocation failure.

- GFX-H2: Changed Bitmap ditherer construction to non-throwing allocation and return OomRowBuffer when the ditherer object or its row buffers cannot be allocated.

- GFX-H3: Marked GfxRenderer::storeBwBuffer() [[nodiscard]] and changed the EPUB anti-aliasing path to abort grayscale rendering if the BW buffer snapshot cannot be stored.

- GFX-H4: Replaced Arduino String serial command parsing in loop() with a fixed-size C-string buffer and overflow drop handling to avoid heap churn from incoming serial bytes.

- ACT-H4: Removed the unconditional SettingsActivity Back save and guarded toggle saves so SETTINGS.saveToFile() only runs after a setting value actually changes.

- ACT-H5: Added per-setting change tracking in StatusBarSettingsActivity so SETTINGS.saveToFile() runs only when the selected status bar value actually changes.

- ACT-H6: Added watchdog resets around KOReader sync/upload blocking calls, reset during NTP polling, and set explicit 3-second KOReader HTTP timeouts to keep main-task blocking bounded.

- ACT-H2: Replaced EPUB reader std::shared_ptr ownership with unique_ptr in EpubReaderActivity and raw non-owning pointers for bounded sub-activities, section parsing, progress mapping, and thumbnail task context.

- ACT-H3: Replaced XTC reader std::shared_ptr ownership with unique_ptr in XtcReaderActivity and raw non-owning pointers for the chapter selector and thumbnail task context.
