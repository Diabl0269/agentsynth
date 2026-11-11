// PointEditorMatrixShapeRows.cpp -- the matrix rows of the shape editor window (the Custom filter's and LFO's shape
// window): its curve editor, and its morph map, each in the real ShapeListEditor over a Filter in a real graph with an
// undo manager.
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "PointEditorMatrixCurveDriver.h"
#include "PointEditorMatrixHoldHost.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/MorphMap/MorphMapGlide.h"
#include "UI/ModuleViews/ShapeEditor/ShapeListEditor.h"

namespace point_matrix {
namespace {

class ShapeRigHolder {
protected:
    ShapeRigHolder() {
        editor_.setSize(synth::ui::ShapeListEditor::kWidth, synth::ui::ShapeListEditor::kPerNoteHeight);
    }
    synth::ui::test::ShapeRig rig_;
    synth::ui::ShapeListEditor editor_{rig_.adapter, 0};
};

class ShapeWindowCurve
    : private ShapeRigHolder
    , public CurveDriver {
public:
    ShapeWindowCurve() { editor_.getCurveEditor().setModel(looseCurveModel()); }
    // The window draws the whole range 0..1 and lets a step stand upright.
    synth::ui::CurveGeometryConfig geometryConfig() const override {
        synth::ui::CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return config;
    }
    synth::ui::CurveEditorComponent& curve() override { return editor_.getCurveEditor(); }
    int editSteps() const override { return rig_.undo.getEditSerial(); }
};

// The morph map is a 2D map with no left-to-right order: its position is the dot's x, its value the height of the dot.
class Morph
    : private ShapeRigHolder
    , public PointDriver {
public:
    Morph() {
        while (map().getNumDots() < 3)
            editor_.getAddButton().onClick();
    }
    juce::Component& surface() override { return map(); }
    int points() const override { return map().getNumDots(); }
    int selected() const override { return map().getSelected() >= 0 ? 1 : 0; }
    int selectedIndex() const override { return map().getSelected(); }
    bool selectedDot(int point) const override { return map().getSelected() == point; }
    double position(int point) const override { return map().getDots()[(size_t)point].x; }
    double value(int point) const override { return 1.0 - map().getDots()[(size_t)point].y; }
    juce::Point<float> dot(int point) override { return map().getDotCentre(point); }
    int editSteps() const override { return rig_.undo.getEditSerial(); }
    bool ordered() const override { return false; }

    // The keys go to the focused dot's stop; the arrows move the dot themselves (Alt is not part of its map).
    bool key(int code, int mods) override {
        auto* stop = map().getDotStop(std::max(0, map().getSelected()));
        return stop != nullptr && stop->keyPressed(keyOf(code, mods));
    }
    bool moveKey(int code) override { return key(code, 0); }
    std::optional<HoldHooks> hold() override {
        auto* m = &map();
        HoldHooks h;
        h.install = [m, keys = h.keys] { m->setGlideHostForTest(hostOf(keys)); };
        h.tick = [m] { m->glideTickForTest(); };
        h.active = [m] { return m->isGliding(); };
        h.keyUp = [m] { m->keyStateChanged(false); };
        h.tapMs = synth::ui::morph_glide::kTapMs;
        return h;
    }
    // The emptiest place on the plot: the grid cell farthest from every dot and the cursor.
    juce::Point<float> emptySpot() override {
        const auto plot = map().getPlotArea();
        juce::Point<float> best = plot.getCentre();
        float bestGap = -1.0f;
        for (int i = 1; i < 12; ++i)
            for (int j = 1; j < 12; ++j) {
                const juce::Point<float> p(plot.getX() + plot.getWidth() * (float)i / 12.0f,
                                           plot.getY() + plot.getHeight() * (float)j / 12.0f);
                float gap = p.getDistanceFrom(map().getCursorCentre());
                for (int d = 0; d < points(); ++d)
                    gap = std::min(gap, p.getDistanceFrom(dot(d)));
                if (gap > bestGap) {
                    bestGap = gap;
                    best = p;
                }
            }
        return best;
    }
    std::optional<juce::Rectangle<int>> rightClick(int point) override {
        std::optional<juce::Rectangle<int>> area;
        map().showMenu = [&area](juce::PopupMenu&, juce::Rectangle<int> a) { area = a; };
        rightPress(dot(point));
        map().showMenu = nullptr;
        return area;
    }

private:
    synth::ui::MorphMap& map() { return editor_.getMap(); }
    const synth::ui::MorphMap& map() const { return const_cast<Morph*>(this)->map(); }
};

} // namespace

void addShapeWindowRows(std::vector<MatrixRow>& rows) {
    rows.push_back({"Curve editor (shape window)", "", [] { return std::make_unique<ShapeWindowCurve>(); }});
    rows.push_back({"Morph map", "MorphMap", [] { return std::make_unique<Morph>(); }});
}

} // namespace point_matrix
