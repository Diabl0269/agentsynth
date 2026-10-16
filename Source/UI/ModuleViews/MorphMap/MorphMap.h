#pragma once

#include "UI/Layout/KeyboardContextMenu.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::ui {

/** The Filter's morph map: one coloured dot per shape and a dark cursor dot, all on a square map whose
 *  coordinates run 0..1 (x to the right, y downwards). It edits nothing itself; every change is reported
 *  through the callbacks, so the module card and the shape editor window drive it the same way.
 *
 *  Pointer: drag a dot to move it, drag the cursor to blend, click empty space to add a shape there,
 *  double-click a dot to edit it, right-click a dot for Edit / Duplicate / Rename / Delete.
 *  Keyboard: the map, then each dot, then the cursor are Tab stops. On a dot or the cursor the arrows move it
 *  (Shift for a bigger step), Return on a dot edits it, Delete removes it, F2 renames it, Shift+F10 opens its
 *  menu; Escape returns focus to the map. Every key step is one gesture bracket, like a drag.
 *
 *  Cables: a cable dropped on the bottom edge drives Morph X, on the left or right edge Morph Y
 *  (`edgeAt`, `setDropHighlight`, `edgeAnchor`).
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
    enum class Edge { None, Bottom, Left, Right };

    static constexpr float kStep = 0.02f;
    static constexpr float kBigStep = 0.1f;
    static constexpr int kPad = 12;
    static constexpr int kEdgeBand = 10;

    MorphMap();
    ~MorphMap() override;

    // ---------- content (message thread) ----------
    /** Replaces the dots; the selection is clamped into the new list. */
    void setDots(std::vector<Dot> dots);
    const std::vector<Dot>& getDots() const noexcept { return dots_; }
    void setCursor(float x, float y);
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
    /** A click on empty space, in map coordinates. */
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

    // ---------- geometry and cables ----------
    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> toPixels(float x, float y) const;
    juce::Point<float> toMap(juce::Point<float> pixels) const;
    juce::Point<float> getDotCentre(int index) const;
    juce::Point<float> getCursorCentre() const;
    /** The edge band under `point` (this component's coordinates), for a cable in flight. */
    Edge edgeAt(juce::Point<int> point) const;
    /** Where a cable lands on `edge`, this component's coordinates (the middle of that edge). */
    juce::Point<float> edgeAnchor(Edge edge) const;
    /** Lights `edge` as the pending drop target; Edge::None clears it. Returns true when it changed. */
    bool setDropHighlight(Edge edge);

    // ---------- inspection ----------
    /** The text a tooltip shows with the pointer at `point`. */
    juce::String tooltipAt(juce::Point<int> point) const;
    juce::Component* getDotStop(int index) const;
    juce::Component* getCursorStop() const;
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
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    juce::String getTooltip() override;
    bool showContextMenuForKeyboardFocus() override;

private:
    class Stop;
    enum class Drag { None, Dot, Cursor, Empty };

    int dotAt(juce::Point<float> point) const;
    bool cursorAt(juce::Point<float> point) const;
    void rebuildStops();
    void layoutStops();
    void refreshStopNames();
    bool handleStopKey(Stop& stop, const juce::KeyPress& key);
    bool stepStop(Stop& stop, float dx, float dy);
    void moveDot(int index, juce::Point<float> to);
    void moveCursor(juce::Point<float> to);
    void endRename(bool commit);
    void select(int index);
    void takeFocus(juce::Component& target);

    std::vector<Dot> dots_;
    juce::Point<float> cursor_{0.5f, 0.5f};
    bool cursorVisible_ = true;
    bool showLabels_ = false;
    int selected_ = -1;
    Edge dropEdge_ = Edge::None;

    Drag drag_ = Drag::None;
    int dragIndex_ = -1;
    bool dragMoved_ = false;
    bool gestureOpen_ = false;

    std::vector<std::unique_ptr<Stop>> dotStops_;
    std::unique_ptr<Stop> cursorStop_;
    std::unique_ptr<juce::TextEditor> renameEditor_;
    int renameIndex_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphMap)
};

} // namespace synth::ui
