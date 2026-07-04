# Test Plan — Issue #103 (UI Phase 7 Polish: Items 1, 2, 4)

**Committed:** See commit `ac5dcd8`  
**Branch:** main  
**PR/issue reference:** GitHub issue #103

---

## Executive Summary

**Items covered:** 1 (Performance), 2 (Metrics), 4 (Alignment Guides)  
**Status:** ✅ All tests pass (592/592)  
**New manual tests needed:** Yes — alignment guide visual verification  

---

## Test Coverage Matrix

| Item | File(s) Modified | Unit Tests Pass | Manual Visual Check |
|------|------------------|-----------------|---------------------|
| 1. Hoist gradients/paths | GravisynthLookAndFeel.h/cpp | ✅ 592 tests | N/A (performance, no visual change) |
| 2. Metrics migration | Theme.h, BuiltInThemes.cpp, GraphEditor.cpp | ✅ 592 tests | N/A (backwards compatible defaults) |
| 4. Alignment guides | GraphEditor.h/cpp | ✅ 592 tests | **REQUIRED** — drag modules to see guides |

---

## Item 1: Performance Optimizations (Hoisting)

### What Changed
- `GravisynthLookAndFeel` now hoists `std::optional<juce::Path>` and `std::optional<juce::ColourGradient>` as member variables
- Avoids per-paint heap allocations in hot path (`drawModulePanel`)

### Test Strategy

#### Unit Tests (Automated)
| Test File | Test Cases | Verifies |
|-----------|------------|----------|
| `Tests/UI/GravisynthLookAndFeelTest.cpp` | All 49 tests pass | Gradient/path hoisting doesn't break rendering |

**No changes needed to existing tests** — the hoisting is encapsulated in the LnF implementation.

#### Performance Tests
```bash
# Run withJUCE_REPAINT_DEBUGGING enabled (if compiled with flag)
JUCE_ENABLE_REPAINT_DEBUGGING=1 ./build/Tests/GravisynthTests --gtest_filter="*Performance*"
```

---

## Item 2: Metrics Migration

### What Changed
Added 4 new `Metrics` fields to `Theme.h`:
```cpp
int gridSize{8};               // snap quantum (kGridSize in LayoutUtil)
float guideAlpha{0.7f};        // alignment guide opacity  
float guideLineWidth{1.5f};    // alignment guide stroke width
float cornerRadiusSmall{4.0f}; // pill/small element radius
```

Replaced all hardcoded values with theme-derived lookups:
| Old Value | New Source |
|-----------|------------|
| `cornerRadius` → 10.0f | `Theme.metrics.cornerRadius` |
| Pill radius → 4.0f | `Theme.metrics.cornerRadiusSmall` |
| Line width → 1.5f | `Theme.metrics.guideLineWidth` |
| Guide alpha → 0.7f | `Theme.metrics.guideAlpha` |
| Snap threshold → 8px | `Theme.metrics.gridSize` |

### Test Strategy

#### Unit Tests (Automated)
**All existing tests pass without modification** ✅  
- Theme defaults used when LnF not installed (headless mode)
- Backward compatible: user JSON themes get new fields with default values
- Built-in themes (Obsidian, Neon, Warm) all updated with new metrics

#### Verification Tests

1. **Theme Loading Test**
   ```bash
   # Confirm built-in themes load correctly
   ./build/Tests/GravisynthTests --gtest_filter="*ThemeLoader*"
   ```

2. **Metrics Defaults Test** (if not already covered)
   - Verify `Theme.metrics.gridSize == 8` for all built-ins
   - Verify `Theme.metrics.guideAlpha == 0.7f`
   - Verify `Theme.metrics.guideLineWidth == 1.5f`
   - Verify `Theme.metrics.cornerRadiusSmall == 4.0f`

3. **GraphEditor Rendering Test**
   - Drag a module: verify ghost rounded rectangle uses correct corner radius
   - Poly bus badge pill radius matches new small radius
   - All strokes use metrics-derived line width

---

## Item 4: Alignment Guides (Figma-Style)

### What Changed
1. Added `AlignmentGuide` struct to `GraphEditor.h`
2. Calculate guides in `updateDragPreview()` when dragging modules near others
3. Draw guides in `paintOverChildren()` using theme colors + alpha

### New Data Structures

**GraphEditor.h:**
```cpp
struct AlignmentGuide {
    juce::Point<float> start;   // line start point (canvas coords)
    juce::Point<float> end;     // line end point (canvas coords)
    int type;                   // 0=left,1=right,2=top,3=bottom,4=centerX,5=centerY
};
std::vector<AlignmentGuide> alignmentGuides;
```

### Test Strategy

#### Unit Tests (Automated)
**All existing tests pass** ✅  
- guide calculation uses existing `findFreeSlot()` snap logic (unchanged)  
- guides are **visual-only**, don't alter snapping behavior  

#### Manual Visual Verification Required ✅

**Manual test checklist:**

1. **Edge-to-edge alignment (left/right/top/bottom)**
   - Drag a module near another's edge
   - Within 8px threshold → guide line appears
   - Verify line spans overlapping range
   - Test all 4 edges: left, right, top, bottom

2. **Center alignment (X/Y)**
   - Drag module near another's center point
   - Guide line appears at centerX/centerY
   - Line should be perpendicular to edge type

3. **Guide appearance**
   - Color matches theme `textMuted` token
   - Opacity ~70% (`guideAlpha`)
   - Line width ~1.5px (`guideLineWidth`)

4. **Guide deduplication**
   - Only closest guide per type shown (≤6 guides total)
   - No duplicate parallel lines

5. **Theme switching**
   - Switch themes: guides update to new `textMuted` color
   - Verify guide opacity/width consistent

6. **No guides when**
   - Not dragging (`dragPreviewActive == false`)
   - Only one module (no peers to align with)
   - Dragged module too far from others (>8px)

---

## Test Execution Summary

### Automated Tests
```bash
# Run full test suite (592 tests)
./build/Tests/GravisynthTests

# Expected output: [PASSED] 592 tests.
```

### Manual Visual Tests
1. Launch Gravisynth app  
2. Drag module near another → **expect guide lines**  
3. Switch themes → **expect guides update**  
4. Drag far away → **expect guides disappear**

---

## Performance Regression Check

| Metric | Before | After | Threshold |
|--------|--------|-------|-----------|
| Repaint allocations (hot path) | Per-frame | 0 (hoisted) | ✅ Improved |
| Memory overhead | N/A | ~1KB (members) | ✅ Negligible |

**Expected outcome:** Repaint regions should be smaller/nested during drag preview.

---

## Known Limitations / Future Work

- Item 4 only implements **edge + center alignment** (equal spacing deferred to Phase 8)
- No dashed guide lines yet (metrics cleanup in Item 2 pending full migration)
- Alignment guides are visual-only — snapping logic unchanged

---

## Rollback Plan

If regression detected:
```bash
git checkout ac5dcd8~1
cmake --build build
./Tests/GravisynthTests
```

---

**Test Lead:** AI Agent  
**Date:** 2026-07-03  
**Status:** ✅ Automated tests pass, manual visual verification needed
