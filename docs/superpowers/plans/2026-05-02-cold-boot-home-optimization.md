# Cold Boot Home Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce cold boot work before the first usable Home screen without requiring a device flash for initial validation.

**Architecture:** Keep changes local to boot orchestration and the stores already involved in boot. Add compile-time-gated timing logs, remove nonessential work from the Home boot path, and preserve current behavior for reader resume, SD-card failure, and settings/sync flows.

**Tech Stack:** PlatformIO, Arduino ESP32-C3, C++20, ESP-IDF/Arduino logging through existing `LOG_*` macros.

---

## File Structure

- Modify `platformio.ini`: add an optional boot timing compile flag in the debug environment only.
- Modify `src/main.cpp`: add boot timing markers, skip the boot splash for normal Home boot, and stop loading KOReader credentials in setup.
- Modify `lib/KOReaderSync/KOReaderCredentialStore.h`: add lazy-load state and public `ensureLoaded()`.
- Modify `lib/KOReaderSync/KOReaderCredentialStore.cpp`: make credential reads load on demand.
- Modify `src/activities/home/HomeActivity.cpp`: defer recent-book path validation until after the first Home render.
- Use existing build validation: `pio run -e default`.

## Task 1: Boot Timing Instrumentation

**Files:**
- Modify: `platformio.ini`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add debug-only compile flag**

Add `-DENABLE_BOOT_TIMING` to `[env:debug]` build flags only.

- [ ] **Step 2: Add no-op timing helper**

In `src/main.cpp`, add a small helper near the anonymous namespace:

```cpp
#ifdef ENABLE_BOOT_TIMING
unsigned long bootTimingLast = 0;

void bootTimingMark(const char* phase) {
  const unsigned long now = millis();
  LOG_INF("BOOT", "%s: +%lu ms, total=%lu ms", phase, now - bootTimingLast, now);
  bootTimingLast = now;
}
#else
void bootTimingMark(const char*) {}
#endif
```

Initialize `bootTimingLast` at the top of `setup()` when timing is enabled.

- [ ] **Step 3: Mark boot phases**

Add `bootTimingMark(...)` after the major setup phases listed in the design spec.

- [ ] **Step 4: Build**

Run: `pio run -e default`

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini src/main.cpp
git commit -m "Add cold boot timing instrumentation"
```

## Task 2: Skip Boot Splash For Normal Home Boot

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Compute boot destination before rendering boot splash**

Load `APP_STATE` and `RECENT_BOOKS` before choosing the destination. Preserve the current reader-resume safety behavior.

- [ ] **Step 2: Skip `activityManager.goToBoot()` for Home boot**

For normal Home boot, call `activityManager.goHome()` directly after display/font setup. Keep full-screen SD error handling unchanged. For reader resume, keep the existing safety update of `APP_STATE` before launching the reader.

- [ ] **Step 3: Build**

Run: `pio run -e default`

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "Skip boot splash before home screen"
```

## Task 3: Lazy-Load KOReader Credentials

**Files:**
- Modify: `lib/KOReaderSync/KOReaderCredentialStore.h`
- Modify: `lib/KOReaderSync/KOReaderCredentialStore.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Add load state**

Add a private `bool loaded = false;` and public `bool ensureLoaded();` to `KOReaderCredentialStore`.

- [ ] **Step 2: Make load/save state-aware**

Have `loadFromFile()` set `loaded = true` before returning. Have `saveToFile()` write current in-memory values and not force loading first.

- [ ] **Step 3: Load before read operations**

Call `ensureLoaded()` at the start of credential read operations: `getMd5Password()`, `hasCredentials()`, `getBaseUrl()`, and any getter used by settings/sync flows. Mutating operations can call `ensureLoaded()` before changing fields to preserve existing data.

- [ ] **Step 4: Remove setup load**

Remove `KOREADER_STORE.loadFromFile()` from `setup()`.

- [ ] **Step 5: Build**

Run: `pio run -e default`

Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add lib/KOReaderSync/KOReaderCredentialStore.h lib/KOReaderSync/KOReaderCredentialStore.cpp src/main.cpp
git commit -m "Lazy-load KOReader credentials"
```

## Task 4: Defer Recent Book Existence Checks

**Files:**
- Modify: `src/activities/home/HomeActivity.cpp`

- [ ] **Step 1: Stop blocking first Home render on `Storage.exists`**

Change `loadRecentBooks()` to copy recent entries up to the max count without existence checks.

- [ ] **Step 2: Validate after first render**

After the first render, perform the existence checks and refresh Home only if entries were removed.

- [ ] **Step 3: Guard selection**

In `onSelectBook`, check `Storage.exists(path.c_str())` before launching the reader. If the file is missing, reload/refresh recent books and stay on Home.

- [ ] **Step 4: Build**

Run: `pio run -e default`

Expected: build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/activities/home/HomeActivity.cpp
git commit -m "Defer recent book validation on home boot"
```

## Final Verification

- [ ] Run `pio run -e default`
- [ ] Run `git status --short`
- [ ] Report commits created and note that real timing still requires flashing the firmware to hardware.
