# Arrangement View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the toggleable Arrangement View component to orchestrate MIDI patterns and automation for Timeline Modules.

**Architecture:** A new UI component `ArrangementView` is toggled in `MainComponent`. It communicates with the graph via an `ArrangementData` model that tracks track bindings to `TimelineModule` nodes.

**Tech Stack:** C++, JUCE framework.

---

### Task 1: Arrangement View Component Boilerplate
**Files:**
- Create: `Source/UI/ArrangementView.h`
- Create: `Source/UI/ArrangementView.cpp`

- [ ] **Step 1: Implement `ArrangementView` component**
```cpp
// Source/UI/ArrangementView.h
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ArrangementView : public juce::Component {
public:
    ArrangementView() {}
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawText("Arrangement View", getLocalBounds(), juce::Justification::centred);
    }
    void resized() override {}
};
```

- [ ] **Step 2: Commit**
```bash
git add Source/UI/ArrangementView.h Source/UI/ArrangementView.cpp
git commit -m "feat: add ArrangementView boilerplate"
```

### Task 2: MainComponent Integration
**Files:**
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`

- [ ] **Step 1: Add ArrangementView to MainComponent and toggle button**
(Update `resized` logic to toggle between `GraphEditor` and `ArrangementView`.)

- [ ] **Step 2: Commit**
```bash
git add Source/MainComponent.h Source/MainComponent.cpp
git commit -m "feat: integrate ArrangementView into MainComponent"
```

### Task 3: Orchestrator Data Structure
**Files:**
- Create: `Source/ArrangementData.h`

- [ ] **Step 1: Implement data model for Track-to-Module bindings**
```cpp
// Source/ArrangementData.h
#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <map>

struct Track {
    juce::String name;
    juce::AudioProcessorGraph::NodeID timelineNodeID;
};

class ArrangementData {
public:
    void bindTrack(int trackIndex, juce::AudioProcessorGraph::NodeID id) { tracks[trackIndex].timelineNodeID = id; }
private:
    std::map<int, Track> tracks;
};
```

- [ ] **Step 2: Commit**
```bash
git add Source/ArrangementData.h
git commit -m "feat: add ArrangementData model"
```
