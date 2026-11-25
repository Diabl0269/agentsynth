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
    /** The component reaches this far past the control's cell, room for the lifted shadow. */
    static constexpr int kPad = 8;
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
    /** What the outline's rectangle reaches past a caption's words: kCaptionClear of clear room inside the stroke, plus
     * the stroke itself and the room a rounded corner takes back (so the corner's arc, not only its straight sides,
     * stays kCaptionClear from the letters), plus a pixel and a half for the letters' anti-aliasing and the focus ring.
     */
    static constexpr float kCaptionRoom = (float)kCaptionClear + kStroke + kCornerRadius * 0.2929f + 1.5f;
    /** Extra room above and below the words: the whole line of text is painted (a descender is not cut by its box), and
     * a thick focus ring on a rounded corner needs a little more than the sides, which the caption's side inset caps.
     */
    static constexpr float kCaptionRoomAbove = 1.0f;

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
    /** The outline's drawn rectangle, in this component's own coordinates: the hit area, grown to hold the caption's
     * words with kCaptionClear around them. */
    juce::Rectangle<float> getOutlineArea() const;
    juce::Rectangle<float> getGripArea() const;

private:
    void animateHover(bool over);

    juce::String paramId_;
    juce::String caption_;
    bool panelOnly_ = false;
    bool passThrough_ = false;
    juce::Rectangle<float> captionText_;
    float hover_ = 0.0f;
    float lift_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver hoverAnim_;
};

} // namespace synth::ui
