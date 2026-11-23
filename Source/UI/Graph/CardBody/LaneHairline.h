#pragma once

#include "UI/Graph/CardBody/CardBodyLanes.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::cardbody {

/** A lane's 1 px rule (between two lanes, or under a lane's title), in the theme's border colour. The lane grid and
 *  the Parametric EQ card's own lanes draw their rules with this one component. */
class LaneHairline : public juce::Component {
public:
    LaneHairline() {
        setInterceptsMouseClicks(false, false);
        setWantsKeyboardFocus(false);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(synth::theme::themeOf(*this).colors.border);
        g.fillRect(getLocalBounds());
    }
};

/** A lane title's look: small, bold, letter-spaced, centred, never taking the mouse or focus. */
inline void styleLaneTitle(juce::Label& header) {
    header.setComponentID("cardSectionHeader");
    header.setJustificationType(juce::Justification::centred);
    header.setFont(
        juce::Font(juce::FontOptions(kLaneTitleFontHeight, juce::Font::bold).withKerningFactor(kLaneTitleKerning)));
    header.setBorderSize(juce::BorderSize<int>(0));
    header.setInterceptsMouseClicks(false, false);
    header.setWantsKeyboardFocus(false);
}

} // namespace synth::cardbody
