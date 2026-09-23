#pragma once

// Source/UI/MidiRemote/MidiLearnMenu.h -- shared right-click MIDI Learn menu/badge/armed-outline
// helpers, used by every learnable surface (docs/control/midi-remote-ui.md#the-learn-interaction).
// See MidiLearnMenu.cpp for why this holds only the registry-agnostic pieces.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::midilearn {

/** Swallows a right-click before `ButtonBase` ever sees it; left clicks are unaffected. */
template <typename ButtonBase>
class RightClickSafeButton : public ButtonBase {
public:
    using ButtonBase::ButtonBase;

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu())
            return;
        ButtonBase::mouseDown(e);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu())
            return;
        ButtonBase::mouseUp(e);
    }
};

/** Target-kind-agnostic (caller resolves a parameter or an action's name/label). `learn` null
 *  means "nothing to append" -- the headless/host-not-wired no-op every caller needs. `forget`
 *  fires only when `mappingLabel` is non-empty; `editAssignment` omits its menu item when null. */
struct MenuContent {
    juce::String targetName;
    juce::String mappingLabel;
    std::function<void()> learn;
    std::function<void()> forget;
    std::function<void()> editAssignment;
};

/** Appends the doc-exact separator + Learn/Forget/Edit block to `menu`; a no-op when
 *  `content.learn` is null. See MidiLearnMenu.cpp for the exact shape. */
void appendMidiLearnMenuItems(juce::PopupMenu& menu, const MenuContent& content);

/** The Preferences "Show MIDI badges on mapped controls" switch (default on). Message thread only;
 *  MainComponent pushes it on launch and on every settings change. It gates paintMidiMappedBadge()
 *  below, so the module cards, mixer and transport bar all honour it without knowing about it. */
void setMappedBadgesVisible(bool visible) noexcept;
bool areMappedBadgesVisible() noexcept;

/** Caller gates on "is this control mapped"; paints unless the Preferences switch is off. */
void paintMidiMappedBadge(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour badgeColour);

/** The same dot, ignoring the Preferences switch -- for the MIDI Remote panel's own surface cells,
 *  where the dot is part of the panel's content rather than a decoration on someone else's control. */
void paintMidiMappedDot(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour badgeColour);

/** Caller gates on "is this the armed control"; always paints when called, and must be repainted
 *  only by an existing gated timer/poll (Source/UI/CLAUDE.md's animation rule). */
void paintMidiLearnArmedOutline(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour armedColour,
                                double armedSinceMs);

} // namespace synth::ui::midilearn
