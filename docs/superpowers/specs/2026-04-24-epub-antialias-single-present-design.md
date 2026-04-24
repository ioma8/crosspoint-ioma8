# EPUB Antialias Single-Present Design Spec

**Date:** 2026-04-24
**Status:** Approved

---

## Goal

Remove the visible double-render effect when EPUB text anti-aliasing is enabled, while keeping antialias quality the same or better.

The current user-visible problem is that a page appears once in normal black-and-white and then visibly updates again when the grayscale antialias data is displayed. The intended outcome is one clean visible page update per page turn in the EPUB reader antialias path.

---

## Scope

This spec covers only the EPUB reader path with `SETTINGS.textAntiAliasing` enabled.

In scope:

- `src/activities/reader/epub/EpubReaderActivity.cpp`
- the smallest required `GfxRenderer` / `HalDisplay` helper changes
- preserving existing progress and bookmark behavior
- preserving existing image-page special handling unless it directly conflicts with the single-present goal

Out of scope:

- PDF, TXT, XTC, menus, home screen, or global refresh policy redesign
- changing antialiasing quality targets
- changing saved EPUB reading position format

---

## Current Behavior

Today the EPUB antialias path does this:

1. Render the page in normal 1-bit black-and-white into the framebuffer.
2. Call `displayBuffer(...)`, making that base page visible.
3. Save BW state.
4. Re-render the same page into grayscale LSB/MSB buffers.
5. Call `displayGrayBuffer()`, making the antialias pass visible afterward.
6. Restore BW state for future rendering.

This means there are multiple render passes internally and also two visible panel update stages. The visible two-stage present is the issue to remove.

---

## Proposed Approach

Keep the existing multi-pass preparation if needed, but delay the first visible panel update until the final antialiased frame is ready.

New EPUB antialias flow:

1. Render the normal BW page into the framebuffer.
2. Preserve the BW state needed for cleanup and future renders.
3. Build grayscale LSB/MSB buffers completely off-screen.
4. Perform one final visible present for the completed antialiased page.
5. Restore or reconstruct the framebuffer state needed for the next frame without a second user-visible page redraw.

The key rule is simple:

> In the EPUB antialias path, `displayBuffer(...)` must not be called before the grayscale buffers for the same page are ready.

---

## Architecture

### EPUB Reader Responsibility

`EpubReaderActivity` remains responsible for:

- deciding whether anti-aliasing is active
- choosing the normal EPUB page-turn path versus the antialias path
- rendering page content, bookmark indicator, and status bar in the right order
- deciding whether image-page special handling applies

### Renderer / Display Responsibility

`GfxRenderer` and `HalDisplay` remain responsible for:

- framebuffer storage/restoration
- grayscale LSB/MSB buffer copying
- final grayscale present to the panel
- cleanup of grayscale buffers after the visible present

### Helper API Direction

If the current low-level calls make the EPUB code awkward or error-prone, add one small helper that means "present the already-prepared antialiased frame now."

That helper must stay narrow:

- no refresh-policy redesign
- no new general scene graph or renderer abstraction
- no change to unrelated readers

---

## Data / Control Flow

### Non-image EPUB pages with antialiasing on

1. Prewarm logic remains unchanged.
2. Render the page BW into the framebuffer.
3. Render bookmark indicator and status bar into the same framebuffer.
4. Preserve framebuffer state.
5. Render grayscale LSB pass for page content.
6. Copy LSB grayscale buffers.
7. Render grayscale MSB pass for page content.
8. Copy MSB grayscale buffers.
9. Perform one visible present for the finished antialiased frame.
10. Restore or reconstruct the BW framebuffer for future operations.

### EPUB pages with images and antialiasing on

The current code has special image-page handling with double fast refresh and selective image blanking. That path exists for ghosting control and may conflict with a strict one-present rule.

Design rule:

- first, remove the visible BW-then-grayscale double present for normal antialias pages
- second, preserve the image-page ghosting behavior unless the implementation shows it can also be collapsed safely into one visible present without quality loss

If image pages cannot be safely unified in the first pass, the implementation may keep the existing image-page exception and explicitly document it in the plan.

---

## Quality Requirements

- Text antialias quality must be the same or better than current output.
- No downgrade from grayscale antialiasing to plain BW fallback during normal successful operation.
- No increase in visible flicker relative to the final antialiased state.
- The first visible frame shown to the user in the antialias path must already be the antialiased result.

---

## Error Handling

Safe fallback behavior matters more than preserving the exact antialias pipeline in low-memory or buffer-failure cases.

If the antialias path cannot complete safely:

- do not show a blank page
- do not show a corrupted mixed BW/grayscale frame
- do not introduce an extra visible redraw
- fall back to one clean normal page present for that frame if necessary

This fallback is acceptable only for error handling, not as the normal success path.

---

## Compatibility

Saved book progress must remain compatible.

This work does not require changes to:

- EPUB progress files
- bookmark files
- spine/page position persistence

After flashing, reopening a book should still restore the last reading position exactly as it does now.

---

## Testing

### Functional checks

- EPUB page turn with antialiasing on shows one visible page update instead of the current two-stage effect.
- EPUB page turn with antialiasing off behaves unchanged.
- Opening a book restores the same saved reading position as before.

### Visual checks

- Text quality is unchanged or better on representative text-heavy pages.
- Status bar and bookmark indicator still render correctly.
- No partially-antialiased intermediate visible frame appears.

### Special-case checks

- EPUB pages with images still render correctly.
- Existing image-page refresh behavior is either preserved or intentionally improved without ghosting regression.
- Low-memory fallback path still results in a readable page.

### Validation commands

At minimum, implementation verification should include:

- the project build (`pio run`)
- any available reader/render host tests relevant to touched files
- on-device manual verification for EPUB page turns with antialiasing enabled

---

## Risks

- The current code may rely on the early BW present for panel-state reasons that are not obvious from the activity code alone.
- Image pages may need to remain a special-case path in the first implementation.
- Framebuffer preservation and grayscale cleanup ordering is easy to get subtly wrong, so the change should stay small and localized.

---

## Recommendation

Implement the smallest EPUB-path change that defers the first visible present until the grayscale antialias buffers are ready, adding only a narrow renderer/display helper if the current API makes that sequencing unsafe or unclear.
