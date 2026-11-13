// ModDotPanelLaunch.cpp -- the shared "open a panel in a window beside its anchor" step.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotPanelLaunch.h"

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
    frame->keepOpenOnOutsideClick = options.keepOpenOnOutsideClick;
    frame->onClosed = [frame] { juce::MessageManager::callAsync([frame] { delete frame; }); };
    frame->showOnDesktop();
    return frame;
}

} // namespace synth::ui
