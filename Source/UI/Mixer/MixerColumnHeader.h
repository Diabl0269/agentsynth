#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

// MixerColumnHeader.h -- FRO11 (P9-5, docs/mixer/panel.md#what-the-mixer-shows): the colour swatch + name label +
// click-to-select-macro row shared by MixerColumnComponent, MixerDirectColumn and
// MixerMasterColumn, so the three column kinds don't triplicate the same paint code (root
// CLAUDE.md's "extract a real collaborator class" preference over copy-pasted paint()). Small
// enough to stay header-only, same as the plan's own budget for this file.

namespace synth::ui {

class MixerColumnHeader : public juce::Component {
public:
    MixerColumnHeader() { setInterceptsMouseClicks(true, false); }

    /** An unset (default-constructed, alpha 0) colour paints no swatch -- Direct's header has no
     *  colour of its own (docs/mixer/panel.md#what-the-mixer-shows). */
    void setColour(juce::Colour colour) {
        if (colour_ == colour)
            return;
        colour_ = colour;
        repaint();
    }

    // Named setDisplayName/getDisplayName, not setName/getName -- juce::Component already declares
    // a virtual setName()/getName() pair for its OWN (debug/accessibility) component name, and
    // shadowing it with an unrelated meaning is exactly the kind of trap
    // -Winconsistent-missing-override exists to catch.
    void setDisplayName(const juce::String& name) {
        if (name_ == name)
            return;
        name_ = name;
        repaint();
    }
    juce::String getDisplayName() const { return name_; }

    /** docs/mixer/mixer.md#channels-follow-audio-not-tracks: a small "+R" badge next to the name when this strip is
     * linked to a track. */
    void setLinkedBadgeVisible(bool visible) {
        if (linkedBadgeVisible_ == visible)
            return;
        linkedBadgeVisible_ = visible;
        repaint();
    }

    /** FRO15 (docs/mixer/sends-and-buses.md): a "BUS" badge in place of the linked badge when this column is a
     * group/send bus -- a bus has no track to link to, so the two are mutually exclusive by construction. */
    void setBusBadgeVisible(bool visible) {
        if (busBadgeVisible_ == visible)
            return;
        busBadgeVisible_ = visible;
        repaint();
    }

    /** Fires on a click anywhere in the header background -- docs/mixer/panel.md#what-the-mixer-shows's "clicking a
     * column selects its macro on the canvas". Left null (the default) for Direct/Master, which have no macro of their
     * own to select. */
    std::function<void()> onHeaderClicked;

    void paint(juce::Graphics& g) override {
        const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const auto surface = laf != nullptr ? laf->getTheme().colors.surfaceHi : juce::Colour(0xff232833);
        const auto text = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);
        const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

        g.setColour(surface);
        g.fillRect(getLocalBounds());

        auto bounds = getLocalBounds().reduced(4);
        if (colour_.getAlpha() > 0) {
            auto swatch = bounds.removeFromLeft(10).reduced(0, 2);
            g.setColour(colour_);
            g.fillRoundedRectangle(swatch.toFloat(), 2.0f);
            bounds.removeFromLeft(4);
        }
        if (busBadgeVisible_ || linkedBadgeVisible_) {
            auto badge = bounds.removeFromRight(busBadgeVisible_ ? 26 : 20);
            g.setColour(busBadgeVisible_ ? text.withAlpha(0.7f) : accent);
            g.setFont(juce::Font(juce::FontOptions(10.0f)));
            g.drawText(busBadgeVisible_ ? "BUS" : "+R", badge, juce::Justification::centred, false);
        }
        g.setColour(text);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(name_, bounds, juce::Justification::centredLeft, true);
    }

    void resized() override {}

    // No drag gesture is meaningful on this header (unlike the fader/pan/insert-list siblings), so
    // every mouseUp inside it is a click -- deliberately not gated on juce::MouseEvent::
    // mouseWasClicked(), which reads the real MouseInputSource's own press-tracking state rather
    // than this event's own fields, and so does not answer correctly for a synthetic event built
    // by hand (a real test's mouseDown()/mouseUp() pair, not a live press) the way it does for a
    // genuine user gesture.
    void mouseUp(const juce::MouseEvent&) override {
        if (onHeaderClicked)
            onHeaderClicked();
    }

private:
    juce::Colour colour_; // alpha 0 by default -- see setColour()
    juce::String name_;
    bool linkedBadgeVisible_ = false;
    bool busBadgeVisible_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerColumnHeader)
};

} // namespace synth::ui
