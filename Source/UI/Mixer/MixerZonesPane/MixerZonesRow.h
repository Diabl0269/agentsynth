#pragma once

#include "Mixer/MixerViewDoc.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerZonesRow.h (docs/mixer/panel.md#side-pane-zones-and-visibility): the small components the Zones pane's list is
// built from -- a channel row (swatch, name, eye toggle), a group header, the eye toggle itself and the "Show all"
// link.
namespace synth::ui {

/** The kinds of channel the list tells apart (the Tracks / Buses chips filter on them). */
enum class MixerZoneChannelKind { Track, Bus, Direct, Master };

/** One channel as the Zones pane shows it. */
struct MixerZoneChannel {
    juce::String id; // MixerViewDoc's stable id
    juce::String name;
    juce::Colour colour;
    MixerZoneChannelKind kind = MixerZoneChannelKind::Track;
    synth::MixerZone zone = synth::MixerZone::Scrolling;
    bool hidden = false;
};

inline const synth::theme::Theme* zonesThemeOf(const juce::Component& component) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel());
    return laf != nullptr ? &laf->getTheme() : nullptr;
}

/** An eye that is open while the channel is shown and struck through while it is hidden. A plain click
 *  toggles; an Alt-click asks to show only this channel. */
class MixerZonesEye : public juce::Button {
public:
    MixerZonesEye();

    std::function<void()> onToggle;
    std::function<void()> onSoloShow;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void clicked(const juce::ModifierKeys& mods) override;
};

/** "Show all", drawn as an underlined accent link. */
class MixerZonesLink : public juce::Button {
public:
    explicit MixerZonesLink(const juce::String& text);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
};

/** One channel in the list. Presses anywhere except the eye belong to the pane's row drag. */
class MixerZonesRow : public juce::Component {
public:
    /** The pane's drag hooks; each receives the row's channel id. */
    struct Hooks {
        std::function<void(const juce::String&, const juce::MouseEvent&)> onGrab;
        std::function<void(const juce::MouseEvent&)> onDrag;
        std::function<void(const juce::MouseEvent&)> onRelease;
        std::function<void(const juce::String&)> onToggleHidden;
        std::function<void(const juce::String&)> onSoloShow;
    };

    explicit MixerZonesRow(Hooks hooks);

    void setChannel(const MixerZoneChannel& channel);
    const MixerZoneChannel& getChannel() const noexcept { return channel_; }
    /** The channel-filter text whose matched letters the name paints highlighted (blank = none). */
    void setHighlightQuery(const juce::String& query);
    /** 0..1: how strongly the row is drawn lifted while it is dragged. */
    void setLift(float lift);
    /** Draws the keyboard cursor's outline on this row. */
    void setKeyboardCursor(bool shown);
    bool hasKeyboardCursor() const noexcept { return cursor_; }
    MixerZonesEye& getEyeForTest() noexcept { return eye_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    Hooks hooks_;
    MixerZoneChannel channel_;
    MixerZonesEye eye_;
    juce::String highlightQuery_;
    float lift_ = 0.0f;
    bool cursor_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerZonesRow)
};

/** A group heading ("Left zone", "Scrolling", "Right zone"). */
class MixerZonesGroupHeader : public juce::Component {
public:
    explicit MixerZonesGroupHeader(synth::MixerZone zone);
    synth::MixerZone getZone() const noexcept { return zone_; }
    static juce::String titleFor(synth::MixerZone zone);
    /** The text the heading paints: the title as written, in sentence case. */
    juce::String getDisplayText() const { return titleFor(zone_); }
    void paint(juce::Graphics& g) override;

private:
    synth::MixerZone zone_;
};

} // namespace synth::ui
