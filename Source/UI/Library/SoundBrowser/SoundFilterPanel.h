#pragma once

#include "UI/Controls/RangeSlider/RangeSlider.h"
#include "UI/Library/SoundBrowser/SoundFilterChipGroup.h"
#include "UI/Library/SoundBrowser/SoundFilterModel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>
#include <vector>

namespace synth::ui {

/** The Filters panel of the Sounds view: Kind, Sound, Tempo, Key, Length and Place, each a control that edits one
 *  `SoundFilterModel`. Tab moves from group to group; Esc asks to close the panel. */
class SoundFilterPanel : public juce::Component {
public:
    static constexpr int kPad = 10;

    explicit SoundFilterPanel(SoundFilterModel& model);

    /** The user changed a filter. */
    std::function<void()> onChanged;
    /** Esc was pressed inside the panel. */
    std::function<void()> onCloseRequested;

    /** Brings every control in line with the model (after a chip was removed, or the song key changed). */
    void syncFromModel();
    /** The places the Place menu lists: id and name. */
    void setPlaces(std::vector<std::pair<juce::String, juce::String>> places);
    /** The height the panel needs when `width` wide. */
    int heightForWidth(int width) const;

    SoundFilterChipGroup& kindGroup() { return kind_; }
    SoundFilterChipGroup& soundGroup() { return sound_; }
    SoundFilterChipGroup& nearTempoGroup() { return nearTempo_; }
    SoundFilterChipGroup& songKeyGroup() { return songKey_; }
    RangeSlider& tempoSlider() { return tempo_; }
    juce::ComboBox& keyBox() { return keyBox_; }
    juce::ComboBox& lengthBox() { return lengthBox_; }
    juce::ComboBox& placeBox() { return placeBox_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changed();
    void tempoChanged(double lower, double upper);
    void updateTempoReadout();
    void keyChosen();
    void lengthChosen();
    void placeChosen();
    void layoutRows(int width, bool apply);

    SoundFilterModel& model_;
    SoundFilterChipGroup kind_{"Kind", "one-shots, loops or instruments"};
    SoundFilterChipGroup sound_{"Sound", "drums, bass, keys, vocals, FX or strings"};
    RangeSlider tempo_;
    SoundFilterChipGroup nearTempo_{"Near song tempo", "sounds near the song's tempo"};
    SoundFilterChipGroup songKey_{"Song key", "sounds in the song's key"};
    juce::ComboBox keyBox_, lengthBox_, placeBox_;
    std::vector<std::pair<juce::String, juce::String>> places_;
    bool syncing_ = false;
    int layoutHeight_ = 0;

    struct Label {
        juce::String text;
        juce::Rectangle<int> area;
    };
    std::vector<Label> labels_;
    juce::Rectangle<int> tempoReadout_;
    juce::String tempoReadoutText_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundFilterPanel)
};

} // namespace synth::ui
