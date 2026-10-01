#include "MainComponentShortcutHints.h"

#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/PianoRoll/PianoRollComponent/PianoRollComponent.h"
#include "UI/Timeline/TimelineTransportBar.h"

namespace synth::ui {

// Concern: which of the main window's buttons carry a Cmd-hold shortcut hint. The overlay itself is
// UI/Chrome/ShortcutHint/; this only names the buttons and the shortcut action behind each.

std::unique_ptr<juce::Component> makeMainWindowShortcutHints(juce::Component& host, ShortcutManager& shortcuts,
                                                             MainWindowHintParts parts) {
    auto overlay = std::make_unique<ShortcutHintOverlay>(host, shortcuts);

    for (const auto& [button, actionId] : parts.buttons)
        overlay->addTarget(*button, actionId);

    // Transport bar (inside the Timeline tab): play/stop is Space; the rest are unbound by default,
    // and an unbound action simply gets no hint.
    if (parts.transport != nullptr) {
        overlay->addTarget(parts.transport->getPlayStopButton(), "togglePlayback");
        overlay->addTarget(parts.transport->getRecordButton(), "transportRecord");
        // Bare L is the timeline's loop key; the command-dispatched action is the MIDI-Remote/rebind twin.
        overlay->addTarget(parts.transport->getLoopButton(), "timelineToggleLoop", "transportToggleLoop");
        overlay->addTarget(parts.transport->getMetronomeButton(), "transportToggleMetronome");
    }

    // Piano roll header chips are painted rectangles, not children: each is an area target on the roll, so
    // the roll being closed (hidden) simply gives no bubble.
    if (parts.pianoRoll != nullptr) {
        using Chip = PianoRollComponent::HeaderButtonId;
        const std::pair<Chip, const char*> chips[] = {{Chip::Quantise, "pianoRollQuantise"},
                                                      {Chip::QuantiseLength, "pianoRollQuantiseLength"},
                                                      {Chip::QuantisePitches, "pianoRollQuantisePitches"},
                                                      {Chip::Scale, "pianoRollToggleScalePanel"},
                                                      {Chip::ScaleFilter, "pianoRollToggleScaleFilter"},
                                                      {Chip::Velocity, "pianoRollToggleVelocityLane"}};
        for (const auto& [chip, actionId] : chips)
            overlay->addAreaTarget(
                *parts.pianoRoll,
                [roll = juce::Component::SafePointer<juce::Component>(parts.pianoRoll), chip = chip] {
                    const auto* r = static_cast<PianoRollComponent*>(roll.getComponent());
                    return r != nullptr ? r->getHeaderChipBounds(chip) : juce::Rectangle<int>();
                },
                actionId);
    }

    // Mixer toolbar toggles: Inserts / Sends / EQ.
    if (parts.mixer != nullptr)
        for (const auto section : {MixerSection::Inserts, MixerSection::Sends, MixerSection::Eq})
            overlay->addTarget(parts.mixer->getSectionToggle(section),
                               MixerPanelComponent::sectionToggleActionId(section));

    overlay->setDockSource([dock = parts.dock, statusBar = parts.statusBar, toggle = parts.toggleBottomPanelButton,
                            isOpen = parts.isDockOpen] {
        DockHintInfo info;
        info.open = isOpen ? isOpen() : true;
        info.dock = dock;
        info.statusBar = statusBar;
        info.toggle = toggle;
        info.toggleActionId = "toggleBottomPanel";
        info.toggleLabel = "Show Panel";
        if (dock != nullptr)
            for (const auto& tab : dock->getStripTabs())
                info.tabs.push_back({tab.button, tab.actionId, tab.name});
        return info;
    });
    return overlay;
}

void sampleMainWindowShortcutHints(juce::Component* overlay, const juce::ModifierKeys& mods) {
    if (overlay != nullptr)
        static_cast<ShortcutHintOverlay*>(overlay)->sample(mods);
}

} // namespace synth::ui
