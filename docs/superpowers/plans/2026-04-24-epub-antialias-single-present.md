# EPUB Antialias Single-Present Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the visible double-render effect for EPUB page turns with text anti-aliasing enabled while preserving or improving antialias quality and keeping saved reading position behavior unchanged.

**Architecture:** Keep the existing multi-pass grayscale preparation, but move the first visible present until after the grayscale buffers are ready. Concentrate the sequencing change in the EPUB reader path, preferably through a narrow shared anti-alias helper in `ReaderUtils`, and preserve current image-page special handling unless a safe single-present version falls out naturally.

**Tech Stack:** C++, PlatformIO, ESP32-C3 firmware, `EpubReaderActivity`, `ReaderUtils`, `GfxRenderer`, `HalDisplay`

---

## File Map

- Modify: `src/activities/reader/epub/EpubReaderActivity.cpp`
  - Current EPUB render ordering, including the early `displayBuffer(...)` call and the grayscale antialias pass.
- Modify: `src/activities/reader/shared/ReaderUtils.h`
  - Existing shared anti-alias helper already contains the common grayscale sequencing and is the most likely place for a narrow “prepare then present once” helper.
- Modify: `lib/GfxRenderer/GfxRenderer.h`
  - Only if a tiny helper declaration is needed to make the final present semantics explicit.
- Modify: `lib/GfxRenderer/GfxRenderer.cpp`
  - Only if a tiny helper implementation is needed for a single completed antialiased present or grayscale cleanup path.
- Optional Modify: `src/activities/reader/xtc/XtcReaderActivity.cpp`
  - Only if the final helper extraction would otherwise duplicate logic and the change is obviously mechanical. Skip if it expands scope.
- No data-format changes:
  - `progress.bin`, bookmarks, EPUB cache layout, persisted spine/page state must remain untouched.

## Constraints To Preserve

- `SETTINGS.textAntiAliasing == true` must still produce grayscale antialias output, not a downgraded BW approximation.
- EPUB saved position compatibility must remain unchanged.
- Non-antialias EPUB path must behave the same as before.
- Image-page special handling may remain a first-pass exception if needed to avoid ghosting regression.

---

### Task 1: Lock Down The Current Antialias Present Ordering

**Files:**
- Modify: `src/activities/reader/epub/EpubReaderActivity.cpp`
- Modify: `src/activities/reader/shared/ReaderUtils.h`
- Test/Reference: `src/activities/reader/xtc/XtcReaderActivity.cpp`

- [ ] **Step 1: Inspect the current EPUB antialias path and shared helper before editing**

Run:

```bash
sed -n '860,930p' src/activities/reader/epub/EpubReaderActivity.cpp
sed -n '100,145p' src/activities/reader/shared/ReaderUtils.h
sed -n '190,255p' src/activities/reader/xtc/XtcReaderActivity.cpp
```

Expected:
- EPUB path visibly presents BW before grayscale.
- `ReaderUtils::renderAntiAliased(...)` already wraps grayscale buffer creation.
- XTC path shows a useful alternate cleanup ordering.

- [ ] **Step 2: Write down the exact target sequencing in comments or scratch notes**

Target sequence:

```text
BW render -> preserve framebuffer state -> grayscale LSB/MSB prep -> one visible present -> restore/cleanup
```

Expected:
- You can explain precisely which call currently causes the visible first present and where it will move.

- [ ] **Step 3: Make the minimal code change that removes the early visible present for non-image EPUB AA pages**

Implementation direction:

```cpp
// Pseudocode only — adapt to existing local patterns.
page->render(...);
drawBookmarkIndicatorIfNeeded();
renderStatusBar();

if (SETTINGS.textAntiAliasing && !imagePageWithAA) {
  // Build grayscale before any visible displayBuffer() call.
  ReaderUtils::renderAntiAliased(...);
} else {
  // Existing non-AA / image-AA logic.
}
```

Expected:
- Normal AA EPUB pages no longer call `displayBuffer(...)` before grayscale buffers are ready.

- [ ] **Step 4: Keep image-page AA behavior unchanged unless the diff stays trivial**

Rule:

```text
Do not broaden the change into image ghosting policy work unless the existing branch can be kept clearly correct with the new helper order.
```

Expected:
- The first implementation stays narrow and low-risk.

- [ ] **Step 5: Build to catch compile and signature errors immediately**

Run:

```bash
pio run
```

Expected:
- Build succeeds, or failures point only to the touched AA sequencing code.

- [ ] **Step 6: Commit the narrow sequencing change**

```bash
git add src/activities/reader/epub/EpubReaderActivity.cpp src/activities/reader/shared/ReaderUtils.h lib/GfxRenderer/GfxRenderer.h lib/GfxRenderer/GfxRenderer.cpp
git commit -m "Fix EPUB antialias double present ordering"
```

Expected:
- One focused commit covering the sequencing fix only.

---

### Task 2: Extract A Small Shared Helper Only If EPUB Logic Is Awkward

**Files:**
- Modify: `src/activities/reader/shared/ReaderUtils.h`
- Optional Modify: `lib/GfxRenderer/GfxRenderer.h`
- Optional Modify: `lib/GfxRenderer/GfxRenderer.cpp`

- [ ] **Step 1: Decide whether EPUB can stay readable with only local changes**

Decision rule:

```text
If EPUB ends up manually duplicating grayscale sequencing or cleanup details,
extract one narrow helper. If not, skip this task entirely.
```

Expected:
- Either a clear “skip” decision or a tiny helper scope.

- [ ] **Step 2: If needed, add one narrow helper with a name that describes completed AA presentation**

Example shape:

```cpp
template <typename RenderFn>
void renderAntiAliasedSinglePresent(GfxRenderer& renderer, RenderFn&& renderFn);
```

The helper should:
- store BW buffer
- build LSB/MSB grayscale buffers
- do the final grayscale visible present
- restore BW state

It should not:
- choose EPUB refresh cadence
- manage chapter/page state
- change global panel policy

- [ ] **Step 3: Replace EPUB’s ad hoc sequencing with the helper**

Expected:
- EPUB code reads as high-level intent rather than panel plumbing.

- [ ] **Step 4: Rebuild after helper extraction**

Run:

```bash
pio run
```

Expected:
- Clean build with no accidental regressions from the helper extraction.

- [ ] **Step 5: Commit only if this helper meaningfully improves clarity**

```bash
git add src/activities/reader/shared/ReaderUtils.h lib/GfxRenderer/GfxRenderer.h lib/GfxRenderer/GfxRenderer.cpp src/activities/reader/epub/EpubReaderActivity.cpp
git commit -m "Refine shared EPUB antialias present helper"
```

Expected:
- Skip the commit entirely if no helper extraction was needed.

---

### Task 3: Verify Behavior And Compatibility

**Files:**
- Modify: none unless verification reveals a real bug
- Test: manual on-device EPUB verification

- [ ] **Step 1: Run formatting and whitespace checks**

Run:

```bash
./bin/clang-format-fix -g
git diff --check
```

Expected:
- No formatting drift or whitespace errors in touched files.

- [ ] **Step 2: Run the build again on the exact final tree**

Run:

```bash
pio run
```

Expected:
- Final build succeeds.

- [ ] **Step 3: Flash or otherwise run the firmware on device and verify normal AA EPUB page turns**

Manual verification checklist:

```text
1. Open an EPUB with textAntiAliasing enabled.
2. Turn several text-only pages.
3. Confirm the user sees one visible page update, not BW first then grayscale.
4. Compare text sharpness against current expected AA quality.
```

Expected:
- One visible present per page turn for normal AA pages.
- No quality drop.

- [ ] **Step 4: Verify compatibility and unchanged non-target behavior**

Manual verification checklist:

```text
1. Reopen a previously-read EPUB and confirm it restores the saved position.
2. Turn pages with textAntiAliasing disabled and confirm no behavior change.
3. Test at least one EPUB page with images.
4. Confirm bookmark indicator and status bar still render correctly.
```

Expected:
- Saved positions still restore correctly.
- Non-AA path is unchanged.
- Image pages are at least no worse than before, or any retained exception is explicitly noted.

- [ ] **Step 5: If image pages remain a deliberate exception, document it in code comments or commit message**

Expected:
- Future work is clear without expanding this implementation.

- [ ] **Step 6: Commit the final verified state**

```bash
git add src/activities/reader/epub/EpubReaderActivity.cpp src/activities/reader/shared/ReaderUtils.h lib/GfxRenderer/GfxRenderer.h lib/GfxRenderer/GfxRenderer.cpp
git commit -m "Finalize EPUB antialias single-present flow"
```

Expected:
- Final commit reflects only the verified implementation state.

---

## Plan Review

Local review result:

- Completeness: sufficient for implementation
- Consistency: no internal contradictions
- Scope: focused on a single subsystem and one visible symptom
- YAGNI: no renderer-wide redesign included

No blocking issues found.
