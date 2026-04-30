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

- KOR-M1: Rejected plaintext KOReader `password` JSON fallback during credential decoding; only obfuscated `password_obf` is accepted.

- NET-M2: Added an unchanged-password guard in WifiCredentialStore::addCredential() so reconnecting to an already stored network does not rewrite wifi.json.

- NET-M1: Removed the legacy wifi.bin XOR migration key and disabled legacy binary WiFi credential migration so the static CrossPoint obfuscation key is no longer shipped in firmware.

- NET-M3: Added bounds-checked SettingInfo string field access before reading CrossPointSettings string buffers for the web settings API.
- NET-M4: Added the same bounds check before writing CrossPointSettings string buffers from web settings POST data.
- NET-M5: Removed per-static-string std::string allocation from handlePostSettings() and changed dynamic web string setters to receive const char* from the JSON document.

- ZIP-M2: Checked Print::write() return values in the STORED ZIP streaming path and fail the read when output writes are short.

- EPUB-M3: Added cached image dimension validation against zero and screen bounds before allocating row buffers or entering the cache render loop.

- EPUB-M5: Reserved initial LUT capacity during section creation to avoid repeated reallocations while indexing chapter pages.

- EPUB-M6: Bounded section anchor-map deserialization by limiting entry count and anchor key length before reading corrupt cache data.

- PDF-M1: Replaced xref `firstObj + count` range checks with overflow-safe comparisons before seeking over subsection rows.
- PNG-M2: Guarded PNG non-IDAT chunk skip arithmetic so `chunkLen + 4` cannot wrap before seekCur().
- IMG-M1: Added non-throwing JPEG scaler accumulator allocations with explicit OOM failure handling before row accumulation.
- IMG-M2: Widened PNG scaler rowCount from uint16_t to uint32_t to avoid overflow during downscaling accumulation.

- ACT-M1: Changed EpubReaderFootnotesActivity to copy the footnote vector instead of storing a reference to parent activity state.
- ACT-M3: Split KOReader sync result navigation so Up/Left move to the previous option and Down/Right move to the next option.

- GFX-M2: Widened GfxRenderer::drawPixel() framebuffer byte index to uint32_t to avoid narrowing if display dimensions grow.
- GFX-L1: Reset Bitmap::prevRowY in rewindToData() so rewound reads restart row-dependent dithering state correctly.
- GFX-L2: Added width/height validation in createBmpHeader() before computing row and image sizes.
- PDF-L1: Replaced FreeRTOS-dead getenv-based page-tree debug detection with a constexpr false helper.
- XTC-L1: Rejected non-positive thumbnail heights in Xtc::generateThumbBmp() before scale and malloc calculations.
- ACT-L2: Removed the unused EpubReaderActivity::skipNextButtonCheck field.
- MAIN-L1: Replaced raw BTN_DOWN screenshot chord checks with MappedInputManager::Button::Down.
- MAIN-L2: Widened boot-time power-button calibration values from uint16_t to unsigned long to avoid millis() truncation.
- SET-M3: Changed legacy binary settings version handling to reject only newer unsupported versions so downgrades no longer force a settings reset.
- NET-L3: Logged failed WifiCredentialStore saveToFile() results when updating last-connected SSID, clearing credentials, or resaving migrated JSON.
- HAL-L1: Guarded HalPowerManager::Lock construction and destruction against a null modeMutex before begin() initializes the power manager.
- KOR-L1: Added explicit zeroing of KOReader username/password string storage before clearing credentials.
- EPUB-M4: Rejected EPUB metadata caches with spine counts beyond int16_t index capacity before casting spine indices.
- EPUB-L1: Reserved initial ParsedText word/style/continuation vector capacity before hot-path word accumulation.
- EPUB-L2: Reserved CSS split helper output vectors based on delimiter or whitespace counts before push_back loops.
- EPUB-L3: Added a free-heap pre-check before allocating the spine-to-TOC index vector for EPUB metadata cache generation.
- KOR-M2: ProgressMapper now accepts raw non-owning Epub pointers instead of std::shared_ptr<Epub>, removing atomic refcount overhead from KOReader progress mapping.
- ACT-L3: SettingsActivity no longer saves settings again from the Back exit path after individual setting changes have already been saved.
- SET-M1: Added same-content guards in JSON settings persistence so CrossPointSettings::saveToFile() skips unchanged SD writes.
- SET-M2: Added no-op guards for unchanged recent-book add/update calls and unchanged recent-books JSON output before writing to SD.
- ACT-M2: Replaced WiFi scan deduplication std::map with direct vector find/update logic to avoid red-black tree node and duplicate key allocations.
- GFX-M1: Removed broken reverse-row Floyd-Steinberg diffusion for left-to-right bitmap readers so error is no longer sent to already-processed pixels.
- ACT-L1: Moved the captive-portal DNSServer pointer into CrossPointWebServerActivity ownership via unique_ptr so re-entering the activity cannot leak a file-scope global.
- ACT-L4: Added unchanged-value guards before Calibre and KOReader keyboard confirmations save settings or credential files.
- FS-L1: Root-anchored FsHelpers::normalisePath() by ignoring leading/current-directory traversal and applying final-component ".." handling.
- READER-M1: Changed ReaderBookmarkStore loading to capped chunked reads instead of byte-by-byte SD reads with unbounded reserve.
- NET-L7: Added WebDAV .davtmp path-length validation before creating or cleaning temp upload paths.
- GFX-L4: Replaced linear duplicate scans in Utf8CodepointCollector::add() with lower_bound insertion into a sorted fixed array.
- NET-L4: Replaced CrossPointWebServer::scanFiles() std::function callback with a function pointer plus context pointer to avoid heap-allocated file-list closures.
- GFX-L3: Reworked GfxRenderer::wrappedText() to scan the input with pointers and mutate one reusable line buffer instead of allocating word/testLine strings in the layout loop.
- PDF-L2: Marked the MD5-over-HTTP exposure resolved by the existing KOR-C2 fix, which prevents plaintext Basic Auth from being sent to non-HTTPS KOReader sync URLs.
- RENDER-M1: Replaced EpubReaderMenuActivity per-render progress std::string/std::to_string concatenation with snprintf into a fixed stack buffer.
- PDF-M2: Changed PDF resource and font dictionary resolution to fill fixed-capacity PdfFixedString buffers instead of returning heap-allocated std::string values by value.
- NET-L5: Replaced persistent global Arduino String WebSocket upload state fields with std::string storage and short-lived String adapters only at API boundaries.
