#pragma once

#include "MorphMapGlide.h"
#include "UI/Layout/KeyGlide/KeyGlideHost.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

class ReorderFramePump;

/** The Filter's morph map: one coloured dot per shape and a dark cursor dot, all on a square map whose
 *  coordinates run 0..1 (x to the right, y downwards). It edits nothing itself; every change is reported
 *  through the callbacks, so the module card and the shape editor window drive it the same way.
 *
 *  Pointer: drag a dot to move it, drag the cursor to blend, double-click empty space to add a shape there,
 *  double-click a dot to delete it (one always stays), right-click a dot for Edit / Duplicate / Rename / Delete.
 *  A dot added or restored grows in and a removed one shrinks out, keyed by its name.
 *  Keyboard: the map, then each dot, then the cursor are Tab stops. On a dot or the cursor an arrow moves it one
 *  step (Shift for a bigger one); held, it glides on, slowly at first and faster the longer it is held (two arrows
 *  move diagonally, Shift is faster). Return on a dot edits it, Delete removes it and focuses the next dot, F2
 *  renames it, Shift+F10 opens its menu; Escape returns focus to the map. A key step or a whole hold is one gesture
 *  bracket, like a drag.
 *
 *  Sliders: a thin Morph X slider hugs the bottom edge and a Morph Y slider the left edge, each with its thumb at
 *  the cursor; the right edge is left empty for a future third axis. They are the cursor's other handles (the same
 *  callbacks, one coordinate at a time) and the landing widgets of the card's Morph X / Morph Y cables
 *  (`stripAt`, `stripAnchor`, `setDropHighlight`). Their Tab stops follow the cursor's; the arrows move them.
 *  Beside each slider runs a thin modulation bar, like a card fader's, that shows the modulation a cable
 *  brings (`setStripModulation`); the cable's landing dot sits just past it. A press on the bar or the dot is
 *  offered to `pressGesture` first, so the owner can turn it into the modulation gestures of a fader.
 */
class MorphMap
    : public juce::Component
    , public juce::TooltipClient
    , public KeyboardContextMenuProvider {
public:
    struct Dot {
        juce::String name;
        float x = 0.5f;
        float y = 0.5f;
    };
    enum class Gesture { Dot, Cursor };
    enum class Axis { None, X, Y };

    static constexpr float kStep = 0.02f;
    static constexpr float kBigStep = 0.1f;
    static constexpr int kStripThickness = 10;
    /** The modulation bar's thickness, px. */
    static constexpr float kModBarThickness = 3.0f;

    /** One routing's modulation of a slider, as the bar shows it (all 0..1 along the slider). */
    struct StripModulation {
        float baseNorm = 0.0f;  ///< where the slider sits
        float modNorm = 0.0f;   ///< base plus the live CV
        float bandStart = 0.0f; ///< the reachable range
        float bandEnd = 0.0f;
        bool positive = true;      ///< the live CV's sign
        bool bandNegative = false; ///< the range is a pull down from rest
        bool hovered = false;      ///< a cable landing here, or the bar, is hovered
        bool operator==(const StripModulation&) const = default;
    };

    MorphMap();
    ~MorphMap() override;

    // ---------- content (message thread) ----------
    /** Replaces the dots; the selection is clamped into the new list. With `glideMoved` (an undo or redo), a dot that
     *  moved with the count unchanged is drawn gliding from where it was. */
    void setDots(std::vector<Dot> dots, bool glideMoved = false);
    const std::vector<Dot>& getDots() const noexcept { return dots_; }
    void setCursor(float x, float y, bool glide = false);
    /** Where a dot or the cursor is drawn now, in map units (trails its place while a restore glide runs). */
    juce::Point<float> getDrawnDot(int index) const;
    juce::Point<float> getDrawnCursor() const;
    bool isMoveGliding() const noexcept { return moveTween_.isAnimating(); }
    void stepMoveGlideForTest(double ms) { moveTween_.stepForTest(ms); }
    float getMoveAlphaForTest(bool cursor, int dot) const {
        return moveTween_.alphaFor(cursor ? kCursorKey : static_cast<KeyedPointTween::Key>(dot));
    }
    juce::Point<float> getCursor() const noexcept { return cursor_; }
    /** A map with no cursor (a shape list that is not morphed) hides the cursor and its Tab stop. */
    void setCursorVisible(bool visible);
    /** -1 for none. Moving it only repaints; it reports nothing. */
    void setSelected(int index);
    int getSelected() const noexcept { return selected_; }
    /** Draws each dot's name beside it (the large map). */
    void setShowLabels(bool show);

    // ---------- reports (all optional) ----------
    /** Bracket every edit that moves something: a drag, one key step. */
    std::function<void(Gesture)> onGestureStart;
    std::function<void(Gesture)> onGestureEnd;
    std::function<void(float, float)> onCursorMoved;
    std::function<void(int, float, float)> onDotMoved;
    /** A double-click on empty space, in map coordinates. The new dot's stop takes keyboard focus. */
    std::function<void(float, float)> onAddAt;
    /** The user chose this dot (click, focus). */
    std::function<void(int)> onSelect;
    std::function<void(int)> onEditDot;
    std::function<void(int)> onDuplicateDot;
    std::function<void(int)> onDeleteDot;
    std::function<void(int, const juce::String&)> onRenameDot;
    /** Shows a menu; unset opens it as a real popup. A test replaces it to inspect the menu. */
    std::function<void(juce::PopupMenu&)> showMenu;
    /** Whether Delete may remove a dot; unset means yes. */
    std::function<bool()> canDeleteDot;
    /** Offered every press first (`phase` 0, in this component's coordinates); true claims the whole press: the
     *  drag and the release then go to it (phases 1 and 2) and the map does nothing with them. */
    std::function<bool(const juce::MouseEvent&, int phase)> pressGesture;
    /** The slider (or its bar) the pointer is over, Axis::None on leaving. */
    std::function<void(Axis)> onStripHover;
    /** A tooltip for a point the owner covers (a bar, a landing dot); empty leaves the map's own. */
    std::function<juce::String(juce::Point<float>)> extraTooltip;

    // ---------- geometry and cables ----------
    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> toPixels(float x, float y) const;
    juce::Point<float> toMap(juce::Point<float> pixels) const;
    juce::Point<float> getDotCentre(int index) const;
    juce::Point<float> getCursorCentre() const;
    /** The slider strip of `axis` (this component's coordinates); empty for Axis::None. */
    juce::Rectangle<float> getStripBounds(Axis axis) const;
    /** The slider under `point` (this component's coordinates), for a cable in flight or a press. */
    Axis stripAt(juce::Point<float> point) const;
    /** Where a cable lands on the slider of `axis`: past its modulation bar, at its middle, clear of both. */
    juce::Point<float> stripAnchor(Axis axis) const;
    /** The diameter of the dot the canvas paints on `stripAnchor`; the map leaves room for it. */
    void setLandingDotDiameter(float diameter);
    /** The strip beside the slider of `axis` the modulation bar is drawn in, spanning the whole slider. */
    juce::Rectangle<float> getModBarTrack(Axis axis) const;
    /** The bar from `fromNorm` to `toNorm` (0..1 along the slider) inside `getModBarTrack`. */
    juce::Rectangle<float> getModBarBetween(Axis axis, float fromNorm, float toNorm) const;
    /** The slider whose modulation bar is under `point` (this component's coordinates). */
    Axis modBarAt(juce::Point<float> point) const;
    /** The modulation each routing on the slider of `axis` brings; empty for none. Repaints only on change. */
    void setStripModulation(Axis axis, std::vector<StripModulation> modulation);
    const std::vector<StripModulation>& getStripModulation(Axis axis) const;
    /** Lights the slider of `axis` as the pending drop target; Axis::None clears it. Returns true when it changed. */
    bool setDropHighlight(Axis axis);

    // ---------- arrow hold ----------
    /** What the arrow hold reads from the outside world; a test replaces it to supply a clock and the keys. */
    using GlideHost = KeyGlideHost;
    void setGlideHostForTest(GlideHost host);
    /** True while an arrow is held on a stop (frames are requested only then). */
    bool isGliding() const noexcept { return glide_.isHeld(); }
    /** One hold frame: the frame pump calls it each VBlank, a test calls it with its own clock. */
    void glideTickForTest() { glideTick(); }

    // ---------- motion ----------
    /** The scale and opacity dot `index` is painted with now (1 and 1 at rest). */
    float getDotScale(int index) const;
    float getDotAlpha(int index) const;
    /** Dots that have been removed and are still shrinking away. */
    int getNumLeavingDots() const noexcept;
    /** True while a grow or shrink runs (frames are requested only then). */
    bool isMoving() const noexcept;
    /** Test seam: lets an off-screen map animate, and steps a running motion to `ms` after its first frame. */
    static void setAnimateOffScreenForTest(bool animate);
    static bool animatesOffScreenForTest();
    void stepMotionForTest(double ms);

    // ---------- inspection ----------
    /** The text a tooltip shows with the pointer at `point`. */
    juce::String tooltipAt(juce::Point<int> point) const;
    juce::Component* getDotStop(int index) const;
    juce::Component* getCursorStop() const;
    juce::Component* getStripStop(Axis axis) const;
    bool isRenaming() const noexcept { return renameEditor_ != nullptr; }
    int getNumDots() const noexcept { return (int)dots_.size(); }

    // ---------- actions the menu and keys share ----------
    void beginRename(int index);
    void showDotMenu(int index);

    // ---------- Component ----------
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    juce::String getTooltip() override;
    bool showContextMenuForKeyboardFocus() override;

private:
    class Stop;
    enum class Drag { None, Dot, Cursor, Strip, Empty };

    int dotAt(juce::Point<float> point) const;
    bool cursorAt(juce::Point<float> point) const;
    void rebuildStops();
    void layoutStops();
    void refreshStopNames();
    bool handleStopKey(Stop& stop, const juce::KeyPress& key);
    bool stepStop(Stop& stop, float dx, float dy);
    bool pressArrow(Stop& stop, int keyCode, bool shift);
    void glideTick();
    void startMoveGlide(const std::vector<Dot>& before, bool glideMoved);
    void endGlide();
    void moveGlideTarget(juce::Point<float> to);
    GlideHost makeDefaultGlideHost() const;
    juce::Point<float> stopPosition(int stopIndex) const;
    void moveDot(int index, juce::Point<float> to);
    void moveCursor(juce::Point<float> to);
    void moveToStrip(Axis axis, juce::Point<float> pixels);
    void paintStrips(juce::Graphics& g) const;
    void paintStripModulation(juce::Graphics& g, Axis axis) const;
    float stripReach() const;
    void setHoveredAxis(Axis axis);
    void endRename(bool commit);
    void select(int index);
    void takeFocus(juce::Component& target);
    void deleteDotAndRefocus(int index);
    void startDotMotion(const std::vector<Dot>& before);
    void motionFrame(double elapsedMs);
    void endMotion();

    /** A dot growing in (restored or added) or shrinking out (removed), by the dot's name. */
    struct DotMotion {
        juce::String id;
        bool leaving = false;
        float progress = 0.0f; ///< 0..1 of this dot's own duration
        Dot dot;               ///< a leaving dot's last state
        int colourIndex = 0;   ///< a leaving dot's colour slot
    };
    const DotMotion* growingMotion(int index) const;

    std::vector<Dot> dots_;
    juce::Point<float> cursor_{0.5f, 0.5f};
    juce::Point<float> grabOffset_; ///< pixels from the pointer to the centre of the dot or cursor being dragged
    bool cursorVisible_ = true;
    bool showLabels_ = false;
    int selected_ = -1;
    Axis dropAxis_ = Axis::None;
    Axis hoverAxis_ = Axis::None;
    float landingDot_ = 7.0f;
    std::vector<StripModulation> stripModulation_[2]; // X, Y
    bool pressClaimed_ = false;

    Drag drag_ = Drag::None;
    int dragIndex_ = -1;
    Axis dragAxis_ = Axis::None;
    bool dragMoved_ = false;
    bool gestureOpen_ = false;

    std::vector<std::unique_ptr<Stop>> dotStops_;
    std::unique_ptr<Stop> cursorStop_;
    std::unique_ptr<Stop> stripStops_[2]; // X, Y
    std::unique_ptr<juce::TextEditor> renameEditor_;
    int renameIndex_ = -1;

    KeyedPointTween moveTween_{*this}; // dots by index, the cursor by kCursorKey
    static constexpr KeyedPointTween::Key kCursorKey = -1;

    std::vector<DotMotion> motions_;
    juce::VBlankAnimatorUpdater motionUpdater_{this};
    AnimationDriver motionDriver_;
    double motionLastMs_ = 0.0;
    bool motionReduced_ = false;

    MorphMapGlide glide_;
    GlideHost glideHost_;
    std::unique_ptr<ReorderFramePump> glidePump_;
    int glideStopIndex_ = -1; // the held stop, by its Stop index
    bool glideGestureOpen_ = false;
    bool glideEnding_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphMap)
};

} // namespace synth::ui
