// Extracted from ModuleComponent (FRO130, which built the module-card-only version of all three
// functions below) so the mixer column and the transport bar (FRO133) call the SAME code instead
// of a second implementation. Deliberately holds only the pieces that need no registry of their
// own -- each surface keeps its OWN MidiLearnableRegistry-shaped type
// (ModuleComponent::MidiLearnableRegistry, MixerColumnComponent's, TimelineTransportBar's),
// because what a registry entry keys on differs per surface (a juce::RangedAudioParameter* for a
// parameter target, a juce::String actionId for an action target) and a shared generic registry
// would cost more in indirection than the ~20 lines of add()/find()/refreshBadges() it would save
// (Source/UI/CLAUDE.md's own "a split class's header declares" reasoning: keep per-surface state
// next to the surface that owns its lifetime).
//
// RightClickSafeButton (header-only, no out-of-line definition to attach this to) lives in the
// header for the same reason it's moved out of ModuleComponent's own ModuleComponentInternal.h (a
// private header not meant for outside includes): every button surface that grows a MIDI Learn
// menu needs it -- juce::Button has no isPopupMenu() guard of its own, so a plain
// ToggleButton/TextButton/DrawableButton fires its click on a RIGHT click exactly as it would on
// a left one, which would fire the mixer's Mute toggle or the transport's Play/Stop while the
// context menu is trying to open.
#include "MidiLearnMenu.h"

#include <cmath>

namespace synth::ui::midilearn {

// Builds:
//   -- (separator)
//   MIDI Learn '<name>'...                          -- unmapped
// or
//   -- (separator)
//   MIDI: <mappingLabel>                            -- disabled title row
//      Edit MIDI assignment...                      -- only when content.editAssignment is set
//      MIDI Learn again...
//      Forget MIDI
void appendMidiLearnMenuItems(juce::PopupMenu& menu, const MenuContent& content) {
    if (!content.learn)
        return; // headless build, mid-teardown, or the host never wired a learn callback

    menu.addSeparator();

    if (content.mappingLabel.isEmpty()) {
        menu.addItem("MIDI Learn '" + content.targetName + "'...", content.learn);
        return;
    }

    menu.addItem(-1, "MIDI: " + content.mappingLabel, false, false); // disabled title row

    if (content.editAssignment)
        menu.addItem("Edit MIDI assignment...", content.editAssignment);

    menu.addItem("MIDI Learn again...", content.learn);

    if (content.forget)
        menu.addItem("Forget MIDI", content.forget);
}

// 6px dot at controlBounds's top-right, in badgeColour (the caller's resolved
// theme.colors.midiMapped, or its fallback constant when no AppLookAndFeel is available -- the
// same idiom every paint() override in this codebase already uses).
void paintMidiMappedBadge(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour badgeColour) {
    constexpr int kBadgeDiameter = 6;
    g.setColour(badgeColour);
    g.fillEllipse(static_cast<float>(controlBounds.getRight() - kBadgeDiameter),
                  static_cast<float>(controlBounds.getY()), static_cast<float>(kBadgeDiameter),
                  static_cast<float>(kBadgeDiameter));
}

// A thin 1px rect around controlBounds, alpha easing ~0.4..1.0 on a 1.2s sine anchored at
// armedSinceMs (juce::Time::getMillisecondCounterHiRes() when this target was armed) --
// deliberately not a glow (docs/control/midi-remote-ui.md#the-learn-interaction: "this state must
// read the same in every theme").
void paintMidiLearnArmedOutline(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour armedColour,
                                double armedSinceMs) {
    const double elapsedSec = (juce::Time::getMillisecondCounterHiRes() - armedSinceMs) / 1000.0;
    constexpr double kBreathPeriodSec = 1.2;
    const float phase = static_cast<float>(
        0.5 * (1.0 - std::cos(2.0 * juce::MathConstants<double>::pi * elapsedSec / kBreathPeriodSec)));
    const float alpha = 0.4f + 0.6f * phase;
    g.setColour(armedColour.withAlpha(alpha));
    g.drawRect(controlBounds, 1);
}

} // namespace synth::ui::midilearn
