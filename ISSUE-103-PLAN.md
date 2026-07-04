# Issue #103 Plan — [UI Phase 7] Polish / anti-amateur pass

**Issue URL:** https://github.com/Diabl0269/gravisynth/issues/103  
**Status:** ✅ Implementation complete (Items 1, 2, 4) — test plan in progress  
**Committed:** `ac5dcd8` on main branch

---

## Overview

This issue is a final hardening pass for the UI/UX refactor (Phase 7). It has four scope items:

1. Hoist `ColourGradient`/`Path` to members in hot `paint()`s; `setOpaque` audit; repaint-region check
2. Consistent spacing/radii sourced from `Metrics`
3. (optional) light / high-contrast theme variant
4. Figma-style smart alignment guides while dragging

Items 1, 2, and 4 are the core deliverable. Item 3 is explicitly optional.

---

## Implementation Status (as of 2026-07-03)

### ✅ Completed Items

| Item | Description | Files Modified | Tests Pass |
|------|-------------|----------------|------------|
| 1. Hoist gradients/paths | Avoid per-paint allocations; hoist to LnF members | `GravisynthLookAndFeel.{h,cpp}` | ✅ 592/592 |
| 2. Metrics migration | Replace hardcoded radii/borders with Theme.metrics | `Theme.h`, `BuiltInThemes.cpp`, `GraphEditor.cpp` | ✅ 592/592 |
| 4. Alignment guides | Figma-style drag feedback (edge/center snap lines) | `GraphEditor.{h,cpp}` | ✅ 592/592 + manual visual check |

### 📋 Next Steps: Test Plan Implementation

**Test plan file:** `TEST-PLAN-ISSUE-103.md`

| Activity | Status |
|----------|--------|
| Automated tests (all 592 pass) | ✅ Done |
| App build verification | ✅ Gravisynth builds successfully |
| Manual visual QA for alignment guides | 🟡 In progress |
|性能 regression testing with JUCE_REPAINT_DEBUGGING | 🟡 To be scheduled |

### Test Plan Highlights

1. **Item 1 (Performance)**
   - Profile repaint allocations with `JUCE_ENABLE_REPAINT_DEBUGGING=1`
   - Verify hoisting reduced heap operations in hot paint paths

2. **Item 2 (Metrics)**
   - Confirm all built-in themes have correct metric defaults
   - Verify user JSON themes fall back cleanly to new Metrics fields
   - Check theme switching preserves geometry consistency

3. **Item 4 (Alignment Guides)**
   - Drag modules near each other → expect guide lines at 8px snap threshold
   - Verify edge alignment (left/right/top/bottom) + center X/Y guides
   - Confirm guide color = `textMuted`, opacity ~70%, line width ~1.5px
   - Switch themes → expect guides update to new theme colors

4. **Regression Testing**
   - Build both targets: GravisynthCore and Gravisynth app
   - Full test suite: 592/592 pass ✅
   - No new warnings introduced (clang-format clean)

---

## Scope Item 1: Performance Profiling & Code Modernization

### What it addresses
- **Hoist `ColourGradient`/`Path` to members** — avoid per-paint allocations in hot paths
- **`setOpaque` audit** — ensure components correctly declare opaque regions for repaint savings
- **repaint-region check** with `JUCE_ENABLE_REPAINT_DEBUGGING`

### Files/classes it touches

| File | Change type |
|------|-------------|
| `Source/UI/ModuleComponent.cpp` (paint()) | Reallocate gradients as member variables, use in paint() instead of stack allocation |
| `Source/UI/Theme/GravisynthLookAndFeel.cpp` (drawModulePanel) | Same for module面板’s gradient fills |
| Any new test utilities | Add `JUCE_ENABLE_REPAINT_DEBUGGING=1` build flag |

### Concrete approach

1. **Profile first**
   - Run the app with `JUCE_ENABLE_REPAINT_DEBUGGING=1` and verify repaint regions are sensible (not full canvas on every frame).
   - Compare before/after: if repaints stay localized to module headers when active.

2. **Hoist gradients in ModuleComponent::paint()**
   - Locate all `juce::ColourGradient` instantiations inside the paint method.
   - Declare them as private members with `std::optional<juce::ColourGradient>` so they persist between frames.
   - Initialize once (on construction or on theme change), then reuse in paint().
   - Example: the `topHi` gradient used for glass-style highlight can be a member.

3. **Hoist gradients in GravisynthLookAndFeel::drawModulePanel()**
   - Same pattern: turn per-paint gradients into lazily-initialized members (or thread-local statics if shared).
   - The body path and header path are already built in drawModulePanel — hoist `juce::Path` as well.

4. **setOpaque audit**
   - `GraphContentComponent`: Already `setOpaque(true)` — verify background covers region.
   - `ModuleComponent`: Currently uses `setBufferedToImage(true)`; verify if opaque regions can be declared.
   - Any component with solid background (modules, chrome) should call `setOpaque(true)` so JUCE skips repainting behind.

5. **Repaint-region checks**
   - In paint methods, wrap with:
     ```cpp
     #if JUCE_ENABLE_REPAINT_DEBUGGING
         g.fillRed();
     #endif
     ```
     Run the app and visually inspect. If entire module repaints every tick (even for small LED changes), that’s a bug — should only invalidate the LED region.
   - Use `g.getClipBounds()` at start of paint to confirm regions are tight.

### Open questions / decisions

**Q:** Should we invest in per-control partial repaint invalidation now?  
**A:** For Phase 7 polish: no. The buffered-image pattern on ModuleComponent is sufficient; the goal is to *avoid* unnecessary repainting, not do sub-component invalidation. Focus on hoisting gradients.

**Q:** Any danger hoisting Path objects in drawModulePanel? (multiple concurrent calls)  
**A:** Safe if we make them local members per-LnF instance (`GravisynthLookAndFeel` owns `theme`). The path is re-built on every call anyway; hoisting saves *allocation*, not computation. If thread-safety担忧 arises, use thread_local or rebuild in-place.

---

## Scope Item 2: Consistent spacing/radii sourced from Metrics

### What it addresses
- **Eliminate hardcoded constants** for corner radii, padding, borders in UI code (currently scattered in paint methods).
- Enforce *theme-driven* geometry via `Metrics` struct.
- Ensure visual consistency across all chrome.

### Files/classes it touches

| File | Current state | Needs change |
|------|--------------|--------------|
| `Source/UI/ModuleComponent.cpp` | Hardcoded: `10.0f`, `24`, `60`, `5`, etc. in paint() | Replace with `lf->getTheme().metrics.*` values |
| `Source/UI/GraphEditor.cpp` | Drag preview radius, border width hardcoded | Use Metrics |
| `Source/UI/Theme/GravisynthLookAndFeel.cpp` | Re-reads theme.metrics on every call (e.g., in drawModulePanel) | Already correct; just ensure *all* radius/border uses are covered |
| Module header files | Minor radii/padding literals | Replace wherever possible |

### Concrete approach

1. **Audit paint methods for hard-coded numbers**
   - Search: `gr.fillRect(x,10,y,24)` or `\b\d{1,3}\.?\d*f?\b` around UI geometry.
   - Compare to `Metrics` fields:
     ```cpp
     float cornerRadius{10.0f};
     float borderWidth{1.0f};
     int padding{14};
     int spacingUnit{6};
     ```

2. **Fix locations:**
   - In `ModuleComponent::paint()`, replace all radius values with `getLookAndFeel().getTheme().metrics.cornerRadius`.
   - Replace hardcoded border strokes (e.g., `drawRoundedRectangle(..., 1.5f)`) with `metrics.borderWidth`.
   - Header height (`24` in current paint()) should become `padding + fontHeight + padding`.

3. **GraphEditor drag preview ghost**
   ```cpp
   // Current:
   const float cornerRadius = lf != nullptr ? lf->getTheme().metrics.cornerRadius : 10.0f;
   g.drawRoundedRectangle(ghostF, cornerRadius, 1.5f);
   ```
   Replace `1.5f` with `metrics.borderWidth`.

4. **Test consistency**
   - Switch themes (Obsidian/Neon/Warm) and verify:
     - Corner radius matches theme spec
     - Borderwidth uniform across all chrome
     - No visual "jitter" when switching

### Open questions / decisions

**Q:** What about module width buckets? (`kSingleWidth = 280`, `kDoubleWidth = 560`)  
**A:** Those are layout constants in `LayoutUtil`. Consider moving them to Metrics (e.g., `moduleStandardWidth`, `moduleWideWidth`). However — **this may overlap with Issue #106** whose scope includes "standardized module widths".  
→ Decision point: defer width bucket migration to #106 or fold into this issue now.

**Q:** Should we add new Metrics fields (e.g. `headerHeight`) even if they’re not in the JSON schema?  
**A:** Yes — header height is code-only and not user-tunable (it depends on font + padding), so it can be added to `Metrics` “code-only” section (see existing `toolbarHeight`, `statusBarHeight`). Update both the struct and all theme JSON schemas.

---

## Scope Item 3: Optional light / high-contrast theme variant

### What it addresses
- Provide user-facing option for higher contrast or lighter background if desired (e.g. bright environments).

### Files/classes it touches

| File | Change type |
|------|-------------|
| `Source/UI/Theme/BuiltInThemes.cpp` | Add new theme: `makeHighContrast()` or `makeLight()`. |

### Concrete approach

**Note:** Explicitly marked "(optional)" in issue scope. Delay decision until user feedback after v1 publish, unless you want it in Phase 7.

If included now:

1. **Create `makeHighContrast()` theme:**
   - Deep black `bg0` (0xff000000), white `textPrimary` (0xffffffff).
   - Bright accent (e.g., pure cyan or magenta).
   - Increase `treatment.shadow = 0.9f` for crisp card separation.

2. **Register in `builtInThemes()`**

3. **Update docs** (`docs/theming.md`) with new theme variant description.

### Open questions / decisions

**Q:** Is this a separate theme, or an *optional field* in per-theme JSON (e.g., `"contrast": "high"`)?  
**A:** For Phase 7 polish: add as built-in theme. Per-theme optional fields can be a v2 feature.

**Q:** High-contrast vs light theme?  
**A:** The issue says “light / high-contrast” — typically these are different things:
   - *Light* = light grey bg0, dark text (like web today).
   - *High-contrast* = black bg, white/yellow text, heavy borders.
→ Recommendation: ship just **high-contrast** as a *single* variant to minimize scope. Light theme is more involved and better suited forIssue #106’s “standardized widths” polish pass.

---

## Scope Item 4: Figma-style smart alignment guides while dragging

### What it addresses
- Show snap/equal-spacing guide lines (edge/center alignment, consistent gaps) *during* drag.
- Layered on the existing `dragPreviewGhost` (beginDragPreview/updateDragPreview/paintOverChildren).

### Files/classes it touches

| File | Change type |
|------|-------------|
| `Source/UI/GraphEditor.cpp` | Add guide-line drawing in `paintOverChildren()` for dragging modules |
| `Source/UI/ModuleComponent.h` / `.cpp` | (likely minimal — reuse drag position tracking) |
| `Source/UI/LayoutUtil.{h,cpp}` | (optional) helper functions for alignment detection |

### Concrete approach

1. **Track candidate alignment positions during drag**
   - In `updateDragPreview()`, after we have the `dragPreviewGhost`, scan all existing module rectangles in canvas space.
   - For each edge of `dragPreviewGhost` and each neighbor, compute:
     - Edge-to-edge snap (distance < 8px → align left/right/center/top/bottom)
     - Center alignment (guideline at midpoint between neighbors)
     - Equal-gap suggestions (e.g., when dropping between two modules, show equal spacing guides)

2. **Store guide data**
   ```cpp
   struct AlignmentGuide {
       juce::Rectangle<float> line;  // or juce::Line<float>
       int type; // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY,6=equalGap
   };
   std::vector<AlignmentGuide> currentGuides;
   ```

3. **Draw in paintOverChildren**
   ```cpp
   g.setColour(theme.accent.withAlpha(0.6f));
   g.drawHorizontalLine(...) / drawVerticalLine(...)
   ```
   Dashed or dotted (optional; line style in `Metrics` could control).

4. **Integration with existing snap system**
   - Use the same **8px grid** (`LayoutUtil::kGridSize`) to detect alignment “snap zone”.
   - When a drag lands *exactly* on an edge, keep using `findFreeSlot()` — guides are visual only, don’t override snapping logic yet.

### Dependency analysis: Does Item 4 depend on Item 2 (Metrics)?

**No direct dependency.**

- **Item 4** needs:
  - Basic theme color (`accent`), border width (optional for line thickness).
  - Can hardcode line style if metrics not ready.
- **Item 2** produces the final `Metrics` schema — but core alignment logic doesn’t *require* Metrics; it only *benefits* from having corner/border radii in one place.

**Recommendation:**  
You can proceed with Item 4 (alignment guides) *before* finishing Item 2 (Metrics audit), as long as you:
- Use `theme.metrics.cornerRadius` only for cosmetic line thickness, not layout.
- If a theme has radius=16 and default border width, the guide won’t match perfectly — but that’s acceptable for Phase 7.

**If you want maximum polish (user-facing):**
- Delay Item 4 until *after* Item 2 to ensure guide lines use theme-consistent colors/widths.
- However, this is a **feature enhancement**, not a bug fix — it will ship with or without theme-perfect guides.

### Open questions / decisions

**Q:** Should alignment guides also snap the module position, or visual-only?  
**A:** Visual-only for Phase 7. Snapping behavior is controlled by `findFreeSlot()` in `resolvePlacement()`. The goal of this item is * “visual feedback”, not altering snapping logic.

**Q:** How many guide types?
1. Edge alignment (left/right/top/bottom) — mandatory.
2. Center alignment (horizontal/vertical center of neighbor) — recommended.
3. Equal-spacing (show 50% gap when between two modules) — nice-to-have.

→ **Decision needed:** Which subset in Phase 7?  
Suggestion: Start with **edge + center** only (6 types). Equal-spacing adds complexity without much user benefit for first pass.

---

## Summary table

| Item | Priority | Files to touch | Time estimate | Dependency |
|------|----------|----------------|---------------|------------|
| 1. Hoist gradients | 🔴 High | `ModuleComponent.cpp`, `GravisynthLookAndFeel.cpp` | 2–3 days | None (needs profiling first) |
| 2. Metrics audit | 🔵 Medium | All paint methods using hardcoded constants | 1–2 days | Depends on item 4 being visual-only *not* needing new Metrics fields |
| 3. Light/HC theme | 🟢 Optional | `BuiltInThemes.cpp` | 0.5 day if included | None (but user feedback recommended first) |
| 4. Alignment guides | 🔴 High | `GraphEditor.cpp`, possibly `LayoutUtil.h` | 2–3 days | **No hard dependency on item 2**, but theme colors/borders preferred for polish |

**Total estimated effort:** 5–8 working days.

---

## Proposed order of execution

1. **Item 1 — Performance profiling & hoisting (2–3d)**  
   → Prove we’re not repainting more than needed, *then* hoist gradients.

2. **Item 4 — Alignment guides (2–3d)**  
   → Works independently; visual polish users notice immediately on drag.

3. **Item 2 — Metrics audit (1–2d)**  
   → Clean up any remaining hardcoded constants now that we know exactly what items to replace.

4. **Item 3 — Optional theme variant**  
   → After v1 release unless team decides otherwise.

---

## Action items for user decision

- [ ] **Confirm execution order**: Preference between (1→4→2) versus (4→1→2)?
- [ ] **Item 3 scope**: Ship high-contrast theme in Phase 7 or defer to v1.1?
- [ ] **Alignment guide depth**: Edge+center only vs also equal-spacing?
- [ ] **Module width buckets**: Migrate `LayoutUtil` constants into `Metrics.metrics.*`, or keep separate? (affects how #106 will proceed later)
