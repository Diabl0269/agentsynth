#pragma once

// One group's handle in the on-card layout editor: the group's title row is what you grab. It spans the whole group
// (so a hover or focus can outline the group), but only the title row takes the mouse, so the control outlines
// inside the group stay reachable. A drag moves the whole group (the editor's), a double-click or Return renames
// it, a right-click offers Rename, Alt+Up and Alt+Down move it. docs/layout/module-card-layout.md#editing-a-layout.

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardLayoutGroupHandle final : public juce::Button {
public:
    CardLayoutGroupHandle(juce::String sectionId, juce::String title);

    const juce::String& getSectionId() const noexcept { return sectionId_; }
    /** Renames the group: the accessible name ("<title> group") and tooltip follow. */
    void setGroupTitle(const juce::String& title);
    /** Places the handle over the group's block (card pixels): `headerHeight` is the title row at its top. */
    void setBlock(juce::Rectangle<int> block, int headerHeight);
    /** True while the group is being dragged: it keeps the solid outline. */
    void setLifted(bool lifted);

    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onDrag;
    std::function<void(const juce::MouseEvent&)> onRelease;
    /** Called with a double-click, Return and the menu's Rename: the title is to be edited. */
    std::function<void()> onRename;
    /** Called first with every key; true = consumed. */
    std::function<bool(const juce::KeyPress&)> onKey;

    /** The right-click menu: Rename group. */
    juce::PopupMenu buildMenu() const;

    bool hitTest(int x, int y) override;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    /** The title row, in this component's own coordinates: the part that takes the mouse. */
    juce::Rectangle<int> getTitleRowArea() const;
    /** The outline round the whole group, in this component's own coordinates. */
    juce::Rectangle<float> getGroupArea() const;
    float getHoverForTest() const noexcept { return hover_; }
    void finishHoverForTest() { hoverAnim_.completeForTest(); }

private:
    void animateHover(bool over);

    juce::String sectionId_;
    juce::String title_;
    int headerHeight_ = 0;
    bool lifted_ = false;
    float hover_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver hoverAnim_;
};

} // namespace synth::ui
