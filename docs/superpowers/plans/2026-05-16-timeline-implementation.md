# Timeline Feature Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a hybrid timeline system featuring a Global Playback HUD and a Timeline Module.

**Architecture:** A singleton `TransportManager` handles global sync state (Play/Pause, Tempo). `TimelineModule` consumes this state to trigger MIDI events.

**Tech Stack:** C++, JUCE framework.

---

### Task 1: Create TransportManager Singleton
**Files:**
- Create: `Source/TransportManager.h`
- Create: `Source/TransportManager.cpp`
- Modify: `Source/AudioEngine.h` (Include and hold instance)

- [ ] **Step 1: Create `TransportManager` class**
```cpp
// Source/TransportManager.h
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

class TransportManager {
public:
    static TransportManager& getInstance();
    
    void play();
    void pause();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void setTempo(float bpm) { tempo.store(bpm); }
    float getTempo() const { return tempo.load(); }
    void setPosition(double samples) { playheadPosition.store(samples); }
    double getPosition() const { return playheadPosition.load(); }

private:
    TransportManager() = default;
    std::atomic<bool> playing{false};
    std::atomic<float> tempo{120.0f};
    std::atomic<double> playheadPosition{0.0};
};
```

- [ ] **Step 2: Commit**
```bash
git add Source/TransportManager.h Source/TransportManager.cpp
git commit -m "feat: setup TransportManager"
```

### Task 2: Implement TimelineModule
**Files:**
- Create: `Source/Modules/TimelineModule.h`

- [ ] **Step 1: Implement `TimelineModule` boilerplate**
```cpp
// Source/Modules/TimelineModule.h
#pragma once
#include "ModuleBase.h"
#include "../TransportManager.h"

class TimelineModule : public ModuleBase {
public:
    TimelineModule() : ModuleBase("Timeline", 0, 1) {} // No inputs, 1 MIDI output
    
    ModuleType getModuleType() const override { return ModuleType::Timeline; }
    
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (!TransportManager::getInstance().isPlaying()) return;
        // Logic to generate MIDI based on playhead position
    }
};
```

- [ ] **Step 2: Commit**
```bash
git add Source/Modules/TimelineModule.h
git commit -m "feat: add TimelineModule scaffold"
```

### Task 3: Global Playback HUD
**Files:**
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`

- [ ] **Step 1: Add Play/Pause UI to MainComponent**
(Add `juce::TextButton` for Play/Pause and connect to `TransportManager`)

- [ ] **Step 2: Commit**
```bash
git add Source/MainComponent.cpp Source/MainComponent.h
git commit -m "feat: add global transport HUD"
```

### Task 4: Integration Tests
**Files:**
- Create: `Tests/TimelineTests.cpp`

- [ ] **Step 1: Test transport state**
```cpp
TEST(TimelineTests, TransportStateTest) {
    auto& tm = TransportManager::getInstance();
    tm.play();
    EXPECT_TRUE(tm.isPlaying());
}
```

- [ ] **Step 2: Commit**
```bash
git add Tests/TimelineTests.cpp
git commit -m "test: add timeline transport tests"
```
