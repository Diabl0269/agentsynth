#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The 14 px marker before a lane header's name: a line with points on it for an automation lane (in the accent
// colour), a wave for a mod lane (in the violet hue). Display only: not a Tab stop and hidden from screen readers (the
// header's own name carries the meaning). Its tooltip is an ordinary info tooltip, so Preferences > "Show info
// tooltips" off silences it. It takes mouse events only to show the tooltip, and hands them on to the header (the
// header adds itself as a mouse listener), so a press on the marker still starts the lane's drag.
class LaneMarkerIcon
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    enum class Kind { Automation, Mod };
    static constexpr int kSize = 14;

    explicit LaneMarkerIcon(Kind kind);

    void setKind(Kind kind);
    Kind getKind() const noexcept { return kind_; }
    void paint(juce::Graphics& g) override;

private:
    Kind kind_;
};

} // namespace synth::ui
