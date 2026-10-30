#pragma once

// The Drum Kit pad's Sound menu (an audio file, a built-in drum, an instrument on the canvas, the loaded kit's
// sounds, or back to the kit sound) and the pad menu around it (Play, Replace from file...). Built for one pad;
// a chosen item id turns back into a Choice. docs/modules/drum-kit.md#a-pads-sound.

#include "Modules/DrumKit/DrumKitModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

class DrumPadSoundMenu {
public:
    struct Choice {
        enum class Kind { None, Play, ChooseFile, File, BuiltIn, KitSound, Instrument, Reset };
        Kind kind = Kind::None;
        int sound = synth::drums::kNoSound;
        juce::String file;
        juce::AudioProcessorGraph::NodeID node;
    };
    struct Instrument {
        juce::AudioProcessorGraph::NodeID node;
        juce::String uuid; // empty until the node is first saved
        juce::String title;
    };

    /** The modules on `graph` a pad can play, in graph order. Null graph: none. */
    static std::vector<Instrument> instrumentsIn(juce::AudioProcessorGraph* graph);
    /** Pad files chosen this session, newest first (at most kMaxRecent). */
    static void rememberFile(const juce::String& path);
    static constexpr int kMaxRecent = 8;
    /** The headings, as a test or a doc names them. */
    static constexpr const char* kFileGroup = "Audio file";
    static constexpr const char* kBuiltInGroup = "Built-in drum sounds";
    static constexpr const char* kMoreDrums = "Hats, toms and cymbals";
    static constexpr const char* kInstrumentGroup = "Instruments on your canvas";
    static constexpr const char* kKitGroup = "Use the kit's sound";

    /** The Sound menu for pad `index` of `kit`. */
    juce::PopupMenu buildSoundMenu(const DrumKitModule& kit, int index, const std::vector<Instrument>& instruments);
    /** Play, Replace from file..., Reset to kit sound, then the Sound groups. */
    juce::PopupMenu buildPadMenu(const DrumKitModule& kit, int index, const std::vector<Instrument>& instruments);
    /** What item `result` of the last built menu means (Kind::None for 0 or an unknown id). */
    Choice choiceFor(int result) const;

private:
    int add(const Choice& choice);
    void addGroups(juce::PopupMenu& menu, const DrumKitModule& kit, int index,
                   const std::vector<Instrument>& instruments);
    void addFileGroup(juce::PopupMenu& menu, const DrumKitModule& kit, int index);
    void addBuiltInGroup(juce::PopupMenu& menu, const synth::drums::Pad& pad);
    void addInstrumentGroup(juce::PopupMenu& menu, const synth::drums::Pad& pad,
                            const std::vector<Instrument>& instruments);
    void addKitGroup(juce::PopupMenu& menu, const DrumKitModule& kit, const synth::drums::Pad& pad);

    std::vector<Choice> choices_;
};

} // namespace synth::ui
