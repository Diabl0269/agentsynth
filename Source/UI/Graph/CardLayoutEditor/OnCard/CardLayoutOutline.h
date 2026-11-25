#pragma once

// One control's outline in the on-card layout editor: a dashed accent outline with a grip in its
// bottom-right corner, solid with a faint wash on hover and keyboard focus, shadowed while lifted. It
// is an accessible button (Return, a double-click or a right-click ask for the control's options through onClick); the
// drag itself is the editor's, fed through the callbacks. docs/layout/module-card-layout.md#editing-a-layout.

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardLayoutOutline final : public juce::Button {
public:
    /** The component reaches this far past the control's cell: room for an outline around a widget that paints to the
     *  cell's edge (kCaptionRoom) and the lifted shadow. */
    static constexpr int kPad = 12;
    /** The outline is drawn this far inside the cell, so abutting cells' outlines never touch. */
    static constexpr int kInset = 1;
    static constexpr int kGripSize = 10;
    /** The clear room the outline keeps around its caption's words on all four sides: the design system's 4 px gap
     * step (kRowCellGap, kFooterRowGap), never a literal. */
    static constexpr int kCaptionClear = 4;
    /** The outline's corner radius. */
    static constexpr float kCornerRadius = 5.0f;
    /** The outline's stroke, drawn inside its rectangle. */
    static constexpr float kStroke = 1.0f;
    /** What the outline's rectangle reaches past a caption's words or a widget's ink: kCaptionClear of clear room
     * inside the stroke, the stroke itself (1.5 px when hovered) and room for anti-aliasing and pixel snapping at 2x.
     * At least kCornerRadius, so a rounded corner's arc is never nearer a content corner than the straight sides are.
     * The focus ring grows outward from the stroke.
     */
    static constexpr float kCaptionRoom = (float)kCaptionClear + kStroke + 2.0f;
    static_assert(kCaptionRoom >= kCornerRadius, "the corner's arc must stay outside the straight sides' clear room");
    /** Extra room above and below a caption's words, beyond kCaptionRoom: none, the words' ink already holds their
     * descenders and the focus ring grows outward. */
    static constexpr float kCaptionRoomAbove = 0.0f;

    CardLayoutOutline(juce::String paramId, juce::String caption);

    /** The component id a scenario finds this outline by (`layoutOutline:<paramId>`), so a drag names the control,
     *  not a pixel that goes stale when a card's layout changes. */
    static juce::String idFor(const juce::String& paramId);

    const juce::String& getParamId() const noexcept { return paramId_; }
    /** Places the outline around the control cell `cell` (card pixels). */
    void setCell(juce::Rectangle<int> cell);
    /** A control with a caption: the words it draws, relative to the cell's top-left. The outline is the cell with
     * that rectangle grown by kCaptionClear on every side, so the text never touches the line. Only the drawing
     * grows: the mouse still hits the cell (getHitArea). */
    void setCaptionText(juce::Rectangle<float> text) {
        captionText_ = text;
        repaint();
    }
    const juce::Rectangle<float>& getCaptionText() const noexcept { return captionText_; }
    /** What the control's widget paints (dial and ticks, value box, pill), relative to the cell's top-left. With it the
     * outline hugs the control: the caption's words and this, each with kCaptionRoom around it, so the gap to the
     * line is the same on all four sides whatever the cell's width. Empty: the outline is the cell. */
    void setWidgetInk(juce::Rectangle<float> ink) {
        widgetInk_ = ink;
        repaint();
    }
    const juce::Rectangle<float>& getWidgetInk() const noexcept { return widgetInk_; }
    /** Renames the control: the accessible title and tooltip follow its caption. */
    void setCaption(const juce::String& caption);
    /** A footer or tab control: no grip, no drag; it only opens its options (Return, double-click, right-click). */
    void setPanelOnly(bool panelOnly);
    bool isPanelOnly() const noexcept { return panelOnly_; }
    /** The card's own Sync switch on an ADSR card: the mouse goes through to the switch under it (it picks the look
     *  being edited); the outline still takes the keyboard (Space flips the switch, Return asks for options). */
    void setPassThrough(bool passThrough);
    bool isPassThrough() const noexcept { return passThrough_; }
    /** 0..1: how far the control is lifted off the card while it is dragged. */
    void setLift(float lift);

    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onDrag;
    std::function<void(const juce::MouseEvent&)> onRelease;
    /** Called first with every key; true = consumed. */
    std::function<bool(const juce::KeyPress&)> onKey;

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

    /** The cell's rectangle the mouse hits, in this component's own coordinates. */
    juce::Rectangle<float> getHitArea() const;
    /** The outline's drawn rectangle, in this component's own coordinates: the caption's words and the widget's ink,
     * each grown by kCaptionRoom (the caption a pixel more above and below); the hit area when neither is known. */
    juce::Rectangle<float> getOutlineArea() const;
    juce::Rectangle<float> getGripArea() const;

private:
    void animateHover(bool over);

    juce::String paramId_;
    juce::String caption_;
    bool panelOnly_ = false;
    bool passThrough_ = false;
    juce::Rectangle<float> captionText_;
    juce::Rectangle<float> widgetInk_;
    float hover_ = 0.0f;
    float lift_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver hoverAnim_;
};

} // namespace synth::ui
