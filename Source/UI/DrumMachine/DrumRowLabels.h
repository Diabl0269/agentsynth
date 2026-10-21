#pragma once

#include "DrumLanes.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <vector>

namespace synth::ui {

/** The drum machine's left column: a coloured dot, the drum's name and its Mute and Solo toggles, one row per lane,
 *  scrolled in step with the cells. The toggles are real buttons so each is a Tab stop with a name. */
class DrumRowLabels : public juce::Component {
public:
    DrumRowLabels() = default;

    struct Row {
        DrumLane lane;
        int top = 0;        ///< y of the row inside this component
        int height = 0;     ///< below the full row height while the row grows in or shrinks out
        float alpha = 1.0f; ///< below 1 while the row fades in or out
        bool muted = false;
        bool soloed = false;
        bool dimmed = false; ///< silenced by another row's solo, or by its own mute
    };

    /** Fired for a user toggle of a row's M or S. */
    std::function<void(int pitch, bool on)> onMuteToggled;
    std::function<void(int pitch, bool on)> onSoloToggled;

    void setRows(std::vector<Row> rows);
    /** The row's toggle, or null when `pitch` has no row. */
    juce::Button* getMuteButton(int pitch) const;
    juce::Button* getSoloButton(int pitch) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    struct Controls {
        std::unique_ptr<juce::TextButton> mute;
        std::unique_ptr<juce::TextButton> solo;
    };

    Controls& controlsFor(const DrumLane& lane);
    void applyThemeColours();
    void layoutControls();

    std::vector<Row> rows_;
    std::map<int, Controls> controls_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumRowLabels)
};

} // namespace synth::ui
