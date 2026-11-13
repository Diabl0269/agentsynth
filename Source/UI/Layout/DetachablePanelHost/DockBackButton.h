#pragma once

#include "UI/Layout/SpaceClaimingButton.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/IconLibrary.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The "Dock back" button: the arrow-into-a-window icon, a Tab stop with the shared focus ring, named and tooltipped
 *  with its key. Space presses it only while it has focus, never as a window-wide shortcut. Owned by whoever hands it
 *  to a panel (a DetachablePanelHost, the Mixer's second window). */
class DockBackButton : public SpaceClaimingButton<juce::DrawableButton> {
public:
    DockBackButton()
        : SpaceClaimingButton<juce::DrawableButton>("dockBack", juce::DrawableButton::ImageFitted) {
        setButtonText({});
        setTitle("Dock back");
        setTooltip("Dock back  (Esc)");
        setWantsKeyboardFocus(true);
    }

    void lookAndFeelChanged() override { applyIcon(); }
    void parentHierarchyChanged() override { applyIcon(); }

private:
    void applyIcon() {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        auto base = lf != nullptr ? lf->getIcon(synth::theme::Icon::ActionDockBack) : nullptr;
        if (base == nullptr)
            return;
        const auto& colors = lf->getTheme().colors;
        auto hover = base->createCopy();
        hover->replaceColour(colors.textMuted, colors.textPrimary);
        setImages(base.get(), hover.get(), hover.get());
    }
};

} // namespace synth::ui
