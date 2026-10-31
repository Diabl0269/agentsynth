#pragma once

// The curve presets every curve box lists: the four built-in shapes, then the user's own, kept as JSON in a file under
// the settings directory. The file is read on every list(), so a box opened later (or a second box) sees a preset
// saved a moment ago. A missing or corrupt file lists the built-ins only.

#include "Modules/ModCurve.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::ui {

class ModCurvePresetStore {
public:
    struct Entry {
        juce::String name;
        synth::ModCurve curve;
        bool builtIn = false;
    };

    /** The store's file is `folder/mod-curve-presets.json`. */
    explicit ModCurvePresetStore(juce::File folder);
    /** The app's own store, under the settings directory (an automation launch keeps it in its temp folder). */
    static ModCurvePresetStore app();

    /** Built-ins first (Linear, Exp, Log, S-curve), then user presets in the order they were first saved. */
    std::vector<Entry> list() const;
    std::vector<Entry> userPresets() const;
    /** Saves `curve` under `name` (trimmed); the same name replaces. False for an empty name, a built-in's name or a
     *  file that cannot be written. */
    bool save(const juce::String& name, const synth::ModCurve& curve) const;
    bool remove(const juce::String& name) const;

    static std::vector<Entry> builtIns();
    juce::File file() const { return file_; }

private:
    bool write(const std::vector<Entry>& user) const;
    juce::File file_;
};

} // namespace synth::ui
