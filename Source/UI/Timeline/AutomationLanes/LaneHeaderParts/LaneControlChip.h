#pragma once

#include "UI/Layout/FadeAmount.h"
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"
#include <functional>
#include <optional>

namespace synth::ui {

// One 22 px icon button on a lane header: a control of the lane's kind, or the "+" / pencil button that follows them.
// A Tab stop with the accent ring; Return and Space press it. Its screen-reader name is the control's name and its
// tooltip "<Name>: <what it does>". In edit mode (setEditing) a press is not a click: the strip tells chips apart
// from drags through the on* callbacks, and the chip is drawn with a grip.
class LaneControlChip : public juce::Button {
public:
    static constexpr int kSize = 22;
    static constexpr int kEditWidth = 26; // a header chip is this wide while the controls are being edited

    /** A chip for `control`; no value makes the "+" / pencil button. */
    LaneControlChip(std::optional<LaneControl> control, LaneControlKind kind);

    std::optional<LaneControl> getControl() const noexcept { return control_; }
    bool isMoreButton() const noexcept { return !control_.has_value(); }

    /** The text the tooltip and the screen-reader name carry after the control's name (the current record mode). */
    void setDetail(const juce::String& detail);
    /** The picture's colour in place of the control's own (Write reads in the error colour). */
    void setIconColour(std::optional<juce::Colour> colour);
    /** Drawn lit: the lane is muted, or the "+" row is open. */
    void setLit(bool lit);
    bool isLit() const noexcept { return lit_; }
    /** Edit mode: a grip, and presses go to the on* callbacks instead of clicking. */
    void setEditing(bool editing);
    bool isEditing() const noexcept { return editing_; }
    /** A chip in the tray (hidden control): dimmed. */
    void setDimmed(bool dimmed);
    /** The "+" button as the pressed pencil of edit mode. */
    void setPencil(bool pencil);
    /** 1 draws at full size; below 1 shrinks the picture about its centre (a chip growing in or shrinking away). */
    void setVisualScale(float scale);
    float getVisualScale() const noexcept { return visualScale_; }
    /** A lifted look while the chip is dragged. */
    void setLifted(bool lifted);

    /** Edit-mode gestures, from the mouse. */
    std::function<void(LaneControlChip&, const juce::MouseEvent&)> onEditPress;
    std::function<void(LaneControlChip&, const juce::MouseEvent&)> onEditDrag;
    std::function<void(LaneControlChip&, const juce::MouseEvent&)> onEditRelease;
    /** Offered every key first (edit mode's Left/Right, Alt+Left/Right, Space and Esc; Esc closing the "+" row): true
     * when used. */
    std::function<bool(LaneControlChip&, const juce::KeyPress&)> onEditKey;
    /** A right-click on the chip: the lane's menu opens at the screen position. */
    std::function<void(juce::Point<int>)> onContextMenu;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void applyNames();

    std::optional<LaneControl> control_;
    LaneControlKind kind_;
    juce::String detail_;
    std::optional<juce::Colour> iconColour_;
    bool lit_ = false;
    bool editing_ = false;
    bool dimmed_ = false;
    bool pencil_ = false;
    bool lifted_ = false;
    float visualScale_ = 1.0f;
};

} // namespace synth::ui
