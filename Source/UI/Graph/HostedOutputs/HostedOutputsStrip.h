#pragma once

// HostedOutputsStrip.h -- what a Hosted Plugin card adds for a plugin with more than one output bus: the "+ N more
// outputs" fold line under the main jacks, the routing button that opens the Outputs panel, and the motion of the aux
// jacks (they slide between the fold line and their rows, and fade as outputs are switched on and off). It owns no
// cables: a cable end asks the card for its jack's place and the card asks this (jackCentre), so a cable leaves from
// the fold line while folded and glides with the jack in between. docs/control/plugin-card-layout.md#outputs-panel.

#include "UI/Layout/PopupHost.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace synth {
class HostedPluginModule;
}

namespace synth::ui {

/** The fold line: "+ N more outputs" folded, "- N more outputs" open. A Tab stop; a click, Return or Space toggles it.
 */
class OutputFoldLine final : public juce::Button {
public:
    OutputFoldLine();

    /** `open` is the amount the line is unfolded (0 folded, 1 open): the plus turns into a minus as it glides. */
    void setState(int count, float open);
    std::function<void()> onToggle;

    bool keyPressed(const juce::KeyPress& key) override;
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void clicked() override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    int count_ = 0;
    float open_ = 0.0f;
};

class HostedOutputsStrip final {
public:
    struct Callbacks {
        /** Re-lays the card at the strip's current rows and tells the canvas its cables moved (every frame). */
        std::function<void()> relayout;
        /** The card's height is final (a glide ended, or it grew at once): neighbours make room or come back. */
        std::function<void()> makeRoom;
        std::function<void()> openPanel;
        /** The fold line was used: flips the fold as one undo step. */
        std::function<void()> toggleFold;
    };

    HostedOutputsStrip(juce::Component& card, HostedPluginModule& module, Callbacks callbacks);
    ~HostedOutputsStrip();

    /** The output set (shown, joined or folded) changed: tween from what is drawn to the new set. `relayoutAtFinal`
     *  re-lays the card (and makes room) with the strip already at its final rows when it grows, so the neighbours
     *  move once; a card not on screen, or Animations Off, lands at once. */
    void modelChanged(const std::function<void()>& relayoutAtFinal);
    /** Lands on the module's current output set with no motion (a new instance, a rebuilt card). */
    void snapToModel();

    // ---- geometry the card reads (card-local) --------------------------------------------------------------------
    /** Where output jack `jack` is, or nullopt when the card places it itself (a main jack, a bare module). */
    std::optional<juce::Point<int>> jackCentre(int jack) const;
    /** 0..1: how much of the jack (dot and label) shows; 1 for a jack the strip does not move. */
    float jackAlpha(int jack) const;
    /** Jacks drawn after the model's own ones: ones just switched off, fading out where they were. */
    int ghostJackCount() const;
    juce::String ghostLabel(int jack) const;
    /** False for an aux jack that is folded, folding or still arriving: only an open one takes a cable. */
    bool jackHittable(int jack) const;
    /** The lowest row the strip needs (card-local y of its centre), or -1 while it is not active. */
    int bottomCentreY() const;
    /** The dot the folded cables leave from, at the fold line's right end. */
    void paintFoldDot(juce::Graphics& g, juce::Colour colour) const;
    bool isActive() const;
    bool isMoving() const { return driver_.isRunning(); }

    // ---- children ------------------------------------------------------------------------------------------------
    /** Places the fold line and the routing button; called from the card's layout pass. */
    void layout();
    /** Shows the routing button on a multi-bus instance; the card hides its Dual I/O button then. */
    void syncButton();
    void setIcon(const juce::Drawable* icon);
    juce::Button& routingButton() { return *button_; }
    OutputFoldLine& foldLine() { return foldLine_; }
    /** The Outputs panel this card has open, if any. */
    PopoverSlot& panelSlot() { return panelSlot_; }

    /** Test seam: advances the glide to progress `e` (0..1) with no VBlank; 1 ends it. */
    void advanceForTest(float e);

private:
    struct Entry {
        int raw = 0;   // the instance's own output channel of the jack's head: what identifies it across changes
        int jack = -1; // its jack in the module's model now; -1 for a ghost (switched off, fading out)
        juce::String label;
        float row = 0.0f, row0 = 0.0f, row1 = 0.0f; // 0 is the first jack's row
        float alpha = 0.0f, alpha0 = 0.0f, alpha1 = 0.0f;
    };

    std::vector<Entry> targetEntries() const;
    int rowY(float row) const;
    float rowNow(const Entry& e) const { return settling_ ? e.row1 : e.row; }
    float alphaNow(const Entry& e) const { return settling_ ? e.alpha1 : e.alpha; }
    float blockNow() const { return settling_ ? block1_ : block_; }
    float openNow() const { return settling_ ? open1_ : open_; }
    float lineAlphaNow() const { return settling_ ? lineAlpha1_ : lineAlpha_; }
    void frame(float e);
    void land();
    void applyTargets(bool animate);
    void refreshLine();

    juce::Component& card_;
    HostedPluginModule& module_;
    Callbacks cb_;
    OutputFoldLine foldLine_;
    PopoverSlot panelSlot_;
    std::unique_ptr<juce::DrawableButton> button_;
    std::unique_ptr<juce::VBlankAnimatorUpdater> updater_;
    AnimationDriver driver_;
    std::vector<Entry> entries_;
    // Rows the aux block (fold line plus open jacks) takes, how open it is, and how much the fold line shows.
    float block_ = 0.0f, block0_ = 0.0f, block1_ = 0.0f;
    float open_ = 0.0f, open0_ = 0.0f, open1_ = 0.0f;
    float lineAlpha_ = 0.0f, lineAlpha0_ = 0.0f, lineAlpha1_ = 0.0f;
    int lineCount_ = 0;
    bool settling_ = false;
};

} // namespace synth::ui
