// MorphShapesCardCurve.cpp -- the card's small curve view (the Filter's Custom type): the selected dot's curve,
// editable in place; while the cursor is the selection, the curve of the dot the cursor sits on, else the blend the
// cursor hears, read-only.
#include "MorphShapesCard.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Layout/ButtonTooltip.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include <cmath>

namespace synth::ui {

namespace {
/** How close the cursor is to a dot, in map units, for the view to edit that dot's shape. */
constexpr float kNearDot = 0.06f;

int nearestShape(const std::vector<ShapeItem>& shapes, juce::Point<float> cursor, float& distance) {
    int best = 0;
    distance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < shapes.size(); ++i) {
        if (shapes[i].position.size() < 2)
            continue;
        const float d = cursor.getDistanceFrom({shapes[i].position[0], shapes[i].position[1]});
        if (d < distance) {
            distance = d;
            best = (int)i;
        }
    }
    return best;
}
} // namespace

void MorphShapesCard::setUpCurveView() {
    curveView_.setTitle("Filter shape curve");
    curveView_.setTooltip("Shape of the selected dot: drag its points to reshape it (double-click empty space opens "
                          "the shape editor). With the morph cursor selected it shows the dot under the cursor; "
                          "between dots it shows the blend the filter uses, read-only");
    curveView_.setVisibleRangeOverride(1.0);
    curveView_.setZeroSegmentPx(0.0f);
    curveView_.setTimeLabelFormatter([](double) { return juce::String(); });
    curveView_.setAxisGuides(adapter_->getAxisGuides());
    curveView_.setFillBaselineLevel(adapter_->getReferenceLevel());
    curveView_.setVisible(false);
    host_.card->addChildComponent(curveView_);

    curveToggle_.setTitle("Show Curve");
    setButtonTooltip(curveToggle_, "Show the shape under the morph cursor as a small curve");
    curveToggle_.onClick = [this] {
        const bool show = curveToggle_.getToggleState();
        host_.rememberView([show](synth::CardViewState& state) { state.showCurveView = show; });
        sync();
    };
    host_.card->addChildComponent(curveToggle_);

    const auto changed = [this] {
        if (curveShape_ < 0)
            syncCurveView(true); // the blend is read-only: put it back
        else
            writeCurveView();
    };
    curveView_.onNodeChanged = [changed](int) { changed(); };
    curveView_.onBendChanged = [changed](int) { changed(); };
    curveView_.onPointsChanged = changed;
    curveView_.onGestureStart = [this] {
        if (curveShape_ < 0)
            return;
        curveGestureOpen_ = true;
        adapter_->beginEdit();
    };
    curveView_.onGestureEnd = [this] {
        if (!curveGestureOpen_)
            return;
        writeCurveView();
        adapter_->endEdit();
        curveGestureOpen_ = false;
    };
    curveView_.onEmptyDoubleClick = [this](const juce::MouseEvent& e) {
        if (curveShape_ < 0 || e.mods.isShiftDown() || getNumShapes() < 2)
            return false;
        openEditor(curveShape_);
        return true;
    };
    curveWatch_ = RestoreWatch(adapter_->restoreSerial());

    // A choice on the map (a click, an arrow, Tab to a dot, the cursor taking the selection) re-reads the view.
    controller_->onSelect = [this](int) { syncCurveView(true); };
    map_.onCursorSelected = [this](bool) { syncCurveView(true); };
}

/** The remembered choice, else shown from two shapes up. */
bool MorphShapesCard::curveWanted() const {
    return module_.getCardViewState().showCurveView.value_or(getNumShapes() >= 2);
}

bool MorphShapesCard::isCurveViewShown() const {
    return host_.curveView && curveFade_.isShown() && curveView_.isVisible();
}

/** The dot the user selected on the map, or -1 while the cursor is the selection (or nothing is). */
int MorphShapesCard::selectedDotForCurve() const {
    const int selected = map_.getSelected();
    return !map_.isCursorSelected() && selected >= 0 && selected < getNumShapes() ? selected : -1;
}

/** Loads the selected dot's curve into the view (while the cursor is the selection: the dot under the cursor, else the
    blend between dots); does nothing while a drag is open or when neither the shapes, the cursor nor the selection
    moved. */
void MorphShapesCard::syncCurveView(bool force) {
    if (!host_.curveView || curveGestureOpen_)
        return;
    if (!curveView_.isVisible()) {
        lastCurveGeneration_ = -1; // loads as soon as the view shows
        return;
    }
    const auto cursor = adapter_->getCursor();
    const int generation = adapter_->getGeneration();
    const int pinned = selectedDotForCurve();
    if (!force && generation == lastCurveGeneration_ && cursor == lastCurveCursor_ && pinned == lastCurvePinned_)
        return;
    const bool selectionChanged = pinned != lastCurvePinned_ && lastCurvePinned_ != -2;
    lastCurveGeneration_ = generation;
    lastCurveCursor_ = cursor;
    lastCurvePinned_ = pinned;

    const auto shapes = adapter_->getShapes();
    if (shapes.empty())
        return;
    float distance = 0.0f;
    const int nearest = pinned >= 0 ? pinned : nearestShape(shapes, cursor, distance);
    const bool onDot = pinned >= 0 || shapes.size() == 1 || distance <= kNearDot;
    const bool restored = curveWatch_.take(adapter_->isRestoring(), adapter_->restoreSerial());
    curveShape_ = onDot ? nearest : -1;
    const auto& curve = onDot ? shapes[(size_t)nearest].curve : adapter_->makeShapeAt(cursor.x, cursor.y).curve;
    // Another dot's curve glides in like an undo does (nodes of a same-shaped curve slide, others snap).
    curveView_.setModel(curveModelFromWave(curve), (restored || selectionChanged) && onDot);
    curveView_.setInterceptsMouseClicks(onDot, onDot);
    curveView_.setWantsKeyboardFocus(onDot);
    describeCurveView(curveShape_, (int)shapes.size());
}

/** The view's screen-reader name says which shape it edits once there is a choice, or that it is the read-only blend.
 */
void MorphShapesCard::describeCurveView(int shape, int count) {
    if (shape < 0) {
        curveView_.setDescription("Filter response blend at the morph cursor, read only");
        return;
    }
    const auto shapes = adapter_->getShapes();
    juce::String text = "Frequency response curve of the filter shape";
    if (count > 1)
        text << (selectedDotForCurve() == shape ? ", editing selected dot " : ", editing ")
             << (shapes[(size_t)shape].name.isEmpty() ? "shape " + juce::String(shape + 1)
                                                      : shapes[(size_t)shape].name);
    curveView_.setDescription(text);
}

/** One edit of the shape on screen goes to the adapter at once; the gesture bracket around it is one undo step. */
void MorphShapesCard::writeCurveView() {
    auto shapes = adapter_->getShapes();
    if (curveShape_ < 0 || curveShape_ >= (int)shapes.size())
        return;
    shapes[(size_t)curveShape_].curve = waveFromCurveModel(curveView_.getModel());
    adapter_->setShapes(shapes);
    lastCurveGeneration_ = adapter_->getGeneration();
}

} // namespace synth::ui
