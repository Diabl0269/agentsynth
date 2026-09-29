#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

namespace synth::ui {

class BottomDockComponent;
class TimelineTransportBar;

/** The main window's parts the Cmd-hold shortcut hints label; MainComponent hands them over once
 *  its children exist (MainComponent::assembleToolbar). */
struct MainWindowHintParts {
    /** Toolbar and status-bar buttons with the shortcut action each one triggers. */
    std::vector<std::pair<juce::Component*, juce::String>> buttons;
    BottomDockComponent* dock{nullptr};
    TimelineTransportBar* transport{nullptr};
    juce::Component* statusBar{nullptr};
    juce::Component* toggleBottomPanelButton{nullptr};
    std::function<bool()> isDockOpen;
};

/** Builds the overlay as a child of `host`. Returned as a plain Component so MainComponent.h needs
 *  no overlay include (its header is at the file-size cap). */
std::unique_ptr<juce::Component> makeMainWindowShortcutHints(juce::Component& host, ShortcutManager& shortcuts,
                                                             MainWindowHintParts parts);

/** Feeds the overlay the live modifier state; MainComponent calls this from its 10 Hz poll and from
 *  modifierKeysChanged(). Null-safe. */
void sampleMainWindowShortcutHints(juce::Component* overlay, const juce::ModifierKeys& mods);

} // namespace synth::ui
