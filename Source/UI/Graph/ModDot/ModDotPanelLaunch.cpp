// ModDotPanelLaunch.cpp -- the shared "open a panel in a window beside its anchor" step.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotPanelLaunch.h"
#include "UI/Layout/AppShortcutRelay.h"

namespace synth::ui {

ModDotPanelFrame* launchInFrame(std::unique_ptr<juce::Component> content, juce::Component& anchor,
                                juce::Rectangle<int> anchorScreenBounds, const ModDotPanelLaunch& options) {
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(anchorScreenBounds);
    const auto area =
        display != nullptr ? display->userArea : juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
    auto* frame = new ModDotPanelFrame(std::move(content), anchor, anchorScreenBounds, area);
    // The window sits outside the app window's tree: it takes the anchor's look (a plugin editor scopes its own) and
    // needs a tooltip window of its own.
    frame->setLookAndFeel(&anchor.getLookAndFeel());
    frame->installTooltipWindow(options.appProperties);
    frame->setAnchorIsRegion(options.anchorIsRegion);
    if (options.setMaxHeight)
        options.setMaxHeight(frame->maxContentHeight());
    if (options.bindDismiss)
        options.bindDismiss([frame = juce::Component::SafePointer<ModDotPanelFrame>(frame)] {
            if (frame != nullptr)
                frame->close();
        });
    // Space, Cmd+Z, Cmd+J...: the window is its own top level, so a key the panel does not use would never reach the
    // app. An undo is seen by the panel through the canvas tick, which re-syncs it or closes it with its connection.
    frame->onAppShortcut = makeAppShortcutRelay(&anchor);
    frame->keepOpenOnOutsideClick = options.keepOpenOnOutsideClick;
    frame->onClosed = [frame] { juce::MessageManager::callAsync([frame] { delete frame; }); };
    frame->showOnDesktop();
    return frame;
}

} // namespace synth::ui
