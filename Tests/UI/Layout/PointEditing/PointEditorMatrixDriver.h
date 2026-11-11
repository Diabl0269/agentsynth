// PointEditorMatrixDriver.h -- what the point editor behaviour matrix drives. One PointDriver per editor (a row), each
// wrapping the real component in its real host and delivering real key and mouse events to its own handlers; the
// probes in PointEditorMatrixProbes.cpp are written once against this interface, so every editor runs the same script.
// A driver never asserts: a probe reports what it saw and the matrix compares that with its expectations.
#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace point_matrix {

constexpr int kShift = juce::ModifierKeys::shiftModifier;
constexpr int kAlt = juce::ModifierKeys::altModifier;
constexpr int kCmd = juce::ModifierKeys::commandModifier;

inline juce::KeyPress keyOf(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

/** The clock and keys a hold sees: the probes set them, the editor's injected key host reads them. */
struct HoldKeys {
    double now = 1000.0;
    std::set<int> down;
    bool shift = false;
    bool alt = false;
};

/** How a key hold is reached on an editor: its injected key host, one frame, and the end of the hold. */
struct HoldHooks {
    std::shared_ptr<HoldKeys> keys = std::make_shared<HoldKeys>();
    std::function<void()> install;
    std::function<void()> tick;
    std::function<bool()> active;
    std::function<void()> keyUp;
    double tapMs = 0.0; ///< the clock offset past which the glide proper runs
};

class PointDriver {
public:
    virtual ~PointDriver() = default;

    virtual juce::Component& surface() = 0;
    /** The points that can be picked, in order along the editor's line (a curve's pinned ends are not points). */
    virtual int points() const = 0;
    /** How many are selected, and the one the keys act on (-1 for none). */
    virtual int selected() const = 0;
    virtual int selectedIndex() const = 0;
    virtual bool selectedDot(int point) const = 0;
    /** Along the ordered axis (time, frequency, map x) and up the value axis (a bigger number is higher on screen). */
    virtual double position(int point) const = 0;
    virtual double value(int point) const = 0;
    /** Where the point is drawn, in the surface's own coordinates. */
    virtual juce::Point<float> dot(int point) = 0;
    /** How many undo steps the edits so far made. */
    virtual int editSteps() const = 0;

    /** A key delivered to whatever takes it (the editor, or the focused dot's stop). */
    virtual bool key(int code, int mods) = 0;
    /** Cmd+D, as it reaches the editor (a lane gets it as the app's edit command). */
    virtual bool duplicate() { return key('D', kCmd); }
    /** The key that moves the selection along `code`'s arrow (Alt+arrow; the morph map's own plain arrow). */
    virtual bool moveKey(int code) { return key(code, kAlt); }
    /** The point the move probes use: one whose position and value can both change. */
    virtual int movePoint() const { return 1; }
    /** Whether the points run in order along one axis (the morph map's do not). */
    virtual bool ordered() const { return true; }
    /** Where the move probes read the point while a key is held (a lane previews before it commits). */
    virtual double heldPosition(int point) const { return position(point); }
    virtual double heldValue(int point) const { return value(point); }
    virtual std::optional<HoldHooks> hold() { return std::nullopt; }
    /** A point of the editor that is empty space for a double-click. */
    virtual juce::Point<float> emptySpot() { return {(dot(0).x + dot(1).x) * 0.5f, 2.0f}; }
    /** Right-clicks a point and returns the screen area the menu it opened would sit at; nullopt when none opened. */
    virtual std::optional<juce::Rectangle<int>> rightClick(int point) = 0;
    /** Drags the point to the edge of the view: "" when the view scrolled with it, else what happened. */
    virtual std::string dragToEdge() { return "the view cannot scroll"; }

    // ---- the mouse, the same for every editor ----
    void press(juce::Point<float> p, int mods = 0) {
        surface().mouseDown(event(p, juce::ModifierKeys::leftButtonModifier | mods, false, p));
        anchor_ = p;
    }
    void dragTo(juce::Point<float> p, int mods = 0) {
        surface().mouseDrag(event(p, juce::ModifierKeys::leftButtonModifier | mods, true, anchor_));
    }
    void release(juce::Point<float> p, int mods = 0) {
        surface().mouseUp(event(p, juce::ModifierKeys::leftButtonModifier | mods, true, anchor_));
    }
    void click(juce::Point<float> p, int mods = 0) {
        press(p, mods);
        surface().mouseUp(event(p, juce::ModifierKeys::leftButtonModifier | mods, false, p));
    }
    void doubleClick(juce::Point<float> p) {
        surface().mouseDoubleClick(event(p, juce::ModifierKeys::leftButtonModifier, false, p));
    }
    void rightPress(juce::Point<float> p) {
        surface().mouseDown(event(p, juce::ModifierKeys::rightButtonModifier, false, p));
    }
    /** Picks one point the way a click does. */
    virtual void selectPoint(int point) { click(dot(point)); }
    /** A box from the top edge left of point `first` to the bottom edge right of point `last`. */
    void box(int first, int last, int mods = 0) {
        const juce::Point<float> from(dot(first).x - 10.0f, 2.0f);
        const juce::Point<float> to(dot(last).x + 10.0f, (float)surface().getHeight() - 2.0f);
        press(from, mods);
        dragTo(to, mods);
        release(to, mods);
    }

protected:
    juce::MouseEvent event(juce::Point<float> p, int mods, bool dragged, juce::Point<float> anchor) {
        auto& comp = surface();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(mods), 0.0f,
                                0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), anchor,
                                juce::Time::getCurrentTime(), 1, dragged);
    }

private:
    juce::Point<float> anchor_;
};

/** One row of the matrix: the editor, the class in Source it is, and a fresh driver for each cell. */
struct MatrixRow {
    std::string name;
    std::string sourceClass; ///< the component class the source scan must find; empty for a wrapper of another row's
    std::function<std::unique_ptr<PointDriver>()> make;
};

void addCurveRows(std::vector<MatrixRow>& rows);
void addLaneRows(std::vector<MatrixRow>& rows);
void addShapeWindowRows(std::vector<MatrixRow>& rows);
void addEqRows(std::vector<MatrixRow>& rows);

/** Every row, in the order the grid prints them. */
const std::vector<MatrixRow>& matrixRows();

} // namespace point_matrix
