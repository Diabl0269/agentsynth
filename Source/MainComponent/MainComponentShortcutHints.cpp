#include "MainComponentShortcutHints.h"

#include "UI/Chrome/ShortcutHint/ShortcutHintOverlay.h"
#include "UI/Layout/BottomDockComponent.h"
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
        overlay->addTarget(parts.transport->getLoopButton(), "transportToggleLoop");
        overlay->addTarget(parts.transport->getMetronomeButton(), "transportToggleMetronome");
    }

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
