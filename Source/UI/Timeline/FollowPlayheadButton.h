#pragma once

#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Timeline/FollowPlayhead.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

// The transport strip's follow toggle. A left click is the toggle (the owner wires onClick); a right click or Shift+F10
// opens a Page / Smooth menu instead, with a check on the current mode, and does not toggle.
class FollowPlayheadButton
    : public juce::DrawableButton
    , public KeyboardContextMenuProvider {
public:
    using juce::DrawableButton::DrawableButton;

    FollowMode mode = FollowMode::Page;
    std::function<void(FollowMode)> onModeChosen;
    // A test replaces the real popup with a capture of the menu.
    std::function<void(juce::PopupMenu&)> showMenuHook;

    juce::PopupMenu buildMenu() const {
        juce::PopupMenu menu;
        menu.addItem(kPageItemId, "Page: jump when the playhead leaves the view", true, mode == FollowMode::Page);
        menu.addItem(kSmoothItemId, "Smooth: keep the playhead pinned while the view scrolls", true,
                     mode == FollowMode::Smooth);
        return menu;
    }

    void showMenu(std::optional<juce::Rectangle<int>> keyboardAnchor = std::nullopt) {
        auto menu = buildMenu();
        if (showMenuHook) {
            showMenuHook(menu);
            return;
        }
        juce::Component::SafePointer<FollowPlayheadButton> safeThis(this);
        menu.showMenuAsync(contextMenuOptions(keyboardAnchor), [safeThis](int result) {
            if (safeThis != nullptr && result != 0 && safeThis->onModeChosen)
                safeThis->onModeChosen(result == kSmoothItemId ? FollowMode::Smooth : FollowMode::Page);
        });
    }

    bool showContextMenuForKeyboardFocus() override {
        showMenu(getScreenBounds());
        return true;
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu())
            showMenu();
        else
            juce::DrawableButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu())
            juce::DrawableButton::mouseUp(e);
    }

    static constexpr int kPageItemId = 1;
    static constexpr int kSmoothItemId = 2;
};

} // namespace synth::ui
