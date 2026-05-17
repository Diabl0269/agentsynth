# MainComponent Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement visibility toggling between `GraphEditor` and `ArrangementView` in `MainComponent`.

**Architecture:**
1.  Add `ArrangementView` instance to `MainComponent`.
2.  Add a `toggleArrangementButton` to the HUD.
3.  Implement visibility state (`isArrangementViewVisible`) and update layout in `resized()`.

**Tech Stack:** JUCE, C++20.

---

### Task 1: Update MainComponent header

**Files:**
- Modify: `Source/MainComponent.h`

- [ ] **Step 1: Include ArrangementView.h**
Add `#include "UI/ArrangementView.h"` to the includes in `Source/MainComponent.h`.

- [ ] **Step 2: Add member variables**
In `private` section of `MainComponent` class:
- Add `ArrangementView arrangementView;`
- Add `juce::TextButton toggleArrangementButton;`
- Add `bool isArrangementViewVisible = false;`

- [ ] **Step 3: Commit**

```bash
git add Source/MainComponent.h
git commit -m "feat: add ArrangementView and toggle button to MainComponent header"
```

### Task 2: Implement toggle logic

**Files:**
- Modify: `Source/MainComponent.cpp`

- [ ] **Step 1: Initialize ArrangementView**
In `MainComponent` constructor:
- Add `addAndMakeVisible(arrangementView);`
- Add `arrangementView.setVisible(isArrangementViewVisible);`

- [ ] **Step 2: Initialize toggle button**
In `MainComponent` constructor:
```cpp
    addAndMakeVisible(toggleArrangementButton);
    toggleArrangementButton.setButtonText("Arrangement");
    toggleArrangementButton.setComponentID("toggleArrangement");
    toggleArrangementButton.onClick = [this] {
        isArrangementViewVisible = !isArrangementViewVisible;
        arrangementView.setVisible(isArrangementViewVisible);
        graphEditor.setVisible(!isArrangementViewVisible);
        toggleArrangementButton.setButtonText(isArrangementViewVisible ? "Graph Editor" : "Arrangement");
        resized();
    };
```

- [ ] **Step 3: Update `resized()`**
In `resized()`:
```cpp
    // ... inside existing header setup ...
    toggleModMatrixButton.setBounds(header.removeFromRight(100).reduced(2));
    toggleArrangementButton.setBounds(header.removeFromRight(120).reduced(2)); // New button
    pauseButton.setBounds(header.removeFromRight(80).reduced(2));
    // ...

    // Position ArrangementView or GraphEditor
    if (isArrangementViewVisible) {
        arrangementView.setBounds(bounds);
    } else {
        graphEditor.setBounds(bounds);
    }
```

- [ ] **Step 4: Update destructor**
Ensure `arrangementView` and `toggleArrangementButton` are handled if necessary (they are members, so JUCE handles them).

- [ ] **Step 5: Commit**

```bash
git add Source/MainComponent.cpp
git commit -m "feat: implement visibility toggle between ArrangementView and GraphEditor"
```

### Task 3: Verify and cleanup

- [ ] **Step 1: Build the project**

Run: `cmake --build build`
Expected: PASS

- [ ] **Step 2: Run tests**

Run: `./build/Tests/GravisynthTests`
Expected: PASS

- [ ] **Step 3: Final Commit**

```bash
git add .
git commit -m "feat: complete MainComponent integration for ArrangementView"
```
