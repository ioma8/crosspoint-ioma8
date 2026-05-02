# Cold Boot Home Optimization Design

## Goal

Reduce perceived cold boot time from power-on or wake to the first usable Home screen.

This work targets the firmware boot path, not EPUB/PDF book opening. It should preserve reliability on the ESP32-C3 and avoid replacing ESP-IDF's allocator unless measurement later shows allocator overhead is the limiting factor.

## Context

The current boot path in `src/main.cpp` performs hardware setup, SD card initialization, panic handling, settings loads, KOReader credential loading, UI theme setup, display/font initialization, boot splash rendering, app state loading, recent-book loading, and then Home activity launch.

Allocator replacement is not the first design choice for this target. ESP-IDF's normal `malloc` and `free` are integrated with its heap capabilities allocator, while third-party general-purpose allocators such as mimalloc are designed mainly for hosted OS-style targets. On ESP32, allocation can depend on memory capabilities and regions, so a global allocator swap would be risky and unlikely to be seamless.

## Approach

Use a measurement-first boot optimization pass:

1. Add lightweight boot timing instrumentation behind a compile-time flag.
2. Measure major phases from early `setup()` through first Home render.
3. Remove or defer work that is not required for first usable Home.
4. Keep each behavior change small and validate with `pio run -e default` after every code change.

## Boot Timing Instrumentation

Add a small boot timing helper local to the firmware, likely in `src/main.cpp` or a tiny utility header if reuse is needed. It should log phase names and elapsed milliseconds only when enabled.

Initial measured phases:

- `HalSystem::begin`, `gpio.begin`, and `powerManager.begin`
- optional USB serial wait
- `Storage.begin`
- panic check and clear
- `SETTINGS.loadFromFile`
- `I18N.loadSettings`
- `KOREADER_STORE.loadFromFile`
- `UITheme::reload`
- `setupDisplayAndFonts`
- `activityManager.goToBoot`
- `APP_STATE.loadFromFile`
- `RECENT_BOOKS.loadFromFile`
- transition to Home or Reader
- first Home render completion

The output should be usable from serial logs in the debug environment and should not add runtime cost to normal builds when disabled.

## Optimization Candidates

### Boot Splash

The boot splash currently renders before the app immediately transitions to Home. On e-ink, an extra display update can dominate perceived startup time.

Design option: make the boot splash conditional. For normal cold boot to Home, skip the splash and render Home as the first screen. Keep the splash for SD-card errors, debug builds, or unusually slow boot paths if measurement shows it still helps user feedback.

### KOReader Credentials

KOReader credentials are loaded during boot, but they are only needed for sync and related settings flows.

Design option: lazy-load KOReader credentials on first use. This removes a JSON read/parse from the Home boot path. The store should keep an internal loaded flag so existing callers can request credentials without duplicating load logic.

### Recent Book Validation

`HomeActivity::loadRecentBooks` filters recent entries by checking `Storage.exists` for each path before first Home render.

Design option: initially display recent entries from the store without blocking on existence checks, then validate them after the first render or when selected. Missing files can be omitted on the next refresh or handled with the existing reader/file error path. This favors first-paint speed while keeping the UI eventually accurate.

### JSON Boot Files

Settings, app state, recent books, and KOReader credentials use JSON. JSON is readable and migration-friendly but may cost boot time through file reads, temporary strings, allocation, and parsing.

Design option: do not change formats in the first pass. If measurement shows JSON parsing dominates, add a binary cache/fast path later while preserving JSON for compatibility and migration.

## Error Handling

Boot timing must never block boot or introduce allocations in failure-sensitive paths. If serial logging is disabled, instrumentation compiles away or becomes no-op.

Lazy loading must preserve current behavior when files are missing or malformed: default values remain usable, errors are logged, and settings screens can still save corrected values.

For deferred recent validation, selecting a missing recent book should fail cleanly and allow the user to return Home. A later implementation can remove missing entries after failed validation if that behavior is already consistent with the recent-books store.

## Testing

Each implementation step should be small and followed by:

```sh
pio run -e default
```

For instrumentation-only changes, verify that default builds compile and debug builds produce phase timing logs when the compile flag is enabled.

For behavior changes, manually compare:

- cold boot to first Home display with current behavior
- cold boot to first Home display after each optimization
- boot with SD card missing or failing
- boot after panic, ensuring crash report handling still runs
- boot with existing recent books, missing recent book files, and KOReader credentials

## Non-Goals

- Replace the global allocator.
- Optimize EPUB/PDF book opening.
- Rewrite all persistence formats in the first pass.
- Refactor unrelated reader, PDF, EPUB, or UI code.

## Recommended Implementation Order

1. Add boot timing instrumentation.
2. Measure current cold boot phases on device.
3. Skip or gate the boot splash if it is a significant cost.
4. Lazy-load KOReader credentials if measurement shows meaningful boot cost.
5. Defer recent-book existence checks if they are measurable.
6. Consider persistence format changes only if JSON parsing remains a top boot cost after the lower-risk changes.
