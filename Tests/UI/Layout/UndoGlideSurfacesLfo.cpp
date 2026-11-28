// UndoGlideSurfacesLfo.cpp -- the registry fixtures for the LFO card's Custom-wave curve editor and the mod curve box's
// added dot (docs/layout/undo-glide.md).
#include "../ModuleViews/ModCurve/ModCurveTestRig.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UndoGlideSurfaces.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace undo_glide_test {

namespace {

using synth::ui::CurveEditorComponent;

constexpr int kNode = 1; // the Triangle default's peak

CurveEditorComponent* findCurveEditor(ModuleComponent& card) {
    for (auto* child : card.getChildren())
        if (auto* curve = dynamic_cast<CurveEditorComponent*>(child))
            return curve;
    return nullptr;
}

juce::MouseEvent mouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged, juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

/** A point of the card's Custom wave, dragged with the mouse; the card picks the undo up on its next poll. */
class LfoCardSurface : public Surface {
public:
    LfoCardSurface() {
        auto* lfo = new LFOModule();
        node_ = engine_.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        editor_ = std::make_unique<GraphEditor>(engine_);
        card_ = std::make_unique<ModuleComponent>(lfo, node_->nodeID, *editor_, &undo_);
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(lfo, "shape")) = LFOModule::kCustomShapeIndex;
        curve_ = findCurveEditor(*card_);
        curve_->getPointTweenForTest().forceAnimateForTest(true);
    }

    void move() override {
        const auto from = geometry().nodePosition(kNode);
        const auto to = from + juce::Point<float>(0.0f, 20.0f);
        curve_->mouseDown(mouseEvent(*curve_, from, false, from));
        curve_->mouseDrag(mouseEvent(*curve_, to, true, from));
        curve_->mouseUp(mouseEvent(*curve_, to, true, from));
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override {
        return geometry().nodePosition(kNode).y + curve_->getPointTweenForTest().offsetFor(kNode).y;
    }
    bool gliding() const override { return curve_->getPointTweenForTest().isAnimating(); }
    void stepTo(double elapsedMs) override { curve_->getPointTweenForTest().stepForTest(elapsedMs); }
    float alpha() const override { return curve_->getPointTweenForTest().alphaFor(kNode); }
    std::vector<DrawnDot> dots() const override { return curveEditorDots(*curve_); }
    juce::Component* paintedComponent() override { return curve_; }

private:
    synth::ui::CurveEditorGeometry geometry() const {
        synth::ui::CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return synth::ui::CurveEditorGeometry(curve_->getModel(), curve_->plotBounds(), config);
    }
    bool restore(bool forward) {
        const bool did = forward ? undo_.redo() : undo_.undo();
        card_->timerCallback(); // the card's generation poll re-syncs the curve
        return did;
    }

    AudioEngine engine_;
    AppUndoManager undo_;
    juce::AudioProcessorGraph::Node::Ptr node_;
    std::unique_ptr<GraphEditor> editor_;
    std::unique_ptr<ModuleComponent> card_;
    CurveEditorComponent* curve_ = nullptr;
};

/** A dot added to the curve box's line through the editor's real double-click; drawn() is the line's height at the
 *  x of the new dot, where the added dot lifts the line well above the straight one it replaced. */
class ModCurveAddedDotSurface : public Surface {
public:
    /** `deleting`: the dot is added first (settled), and the move is the double-click that deletes it again, so the
     *  undo adds a dot back and the redo deletes it. */
    explicit ModCurveAddedDotSurface(bool deleting = false)
        : deleting_(deleting) {
        rig_.box->setSize(320, 260);
        auto& ed = rig_.editor();
        ed.getPointTweenForTest().forceAnimateForTest(true);
        ed.getBendTweenForTest().forceAnimateForTest(true);
        ed.getMorphTweenForTest().forceAnimateForTest(true);
        const auto onLine = rig_.onLine(0.5);
        at_ = {onLine.x, onLine.y - 60.0f};
        if (deleting_) {
            rig_.doubleClick(at_); // the dot the move deletes
            stepTo(1000.0);
            dotAt_ = ed.getGeometryForTest().nodePosition(1);
        }
    }

    void move() override { rig_.doubleClick(deleting_ ? dotAt_ : at_); }
    bool undo() override { return rig_.f.undo.undo(); }
    bool redo() override { return rig_.f.undo.redo(); }
    double drawn() const override {
        const auto points = const_cast<ModCurveAddedDotSurface*>(this)->rig_.editor().getDrawnCurvePointsForTest();
        for (size_t i = 1; i < points.size(); ++i)
            if (points[i].x >= at_.x && points[i].x > points[i - 1].x)
                return points[i - 1].y +
                       (points[i].y - points[i - 1].y) * (at_.x - points[i - 1].x) / (points[i].x - points[i - 1].x);
        return points.empty() ? 0.0 : points.back().y;
    }
    bool gliding() const override {
        return const_cast<ModCurveAddedDotSurface*>(this)->rig_.editor().getMorphTweenForTest().isAnimating();
    }
    void stepTo(double elapsedMs) override {
        auto& ed = rig_.editor();
        ed.getMorphTweenForTest().stepForTest(elapsedMs);
        ed.getPointTweenForTest().stepForTest(elapsedMs);
        ed.getBendTweenForTest().stepForTest(elapsedMs);
    }
    float alpha() const override {
        return const_cast<ModCurveAddedDotSurface*>(this)->rig_.editor().getMorphTweenForTest().alphaFor(0);
    }
    juce::Component* paintedComponent() override { return &rig_.editor(); }
    std::vector<DrawnDot> dots() const override { return curveEditorDots(const_cast<BoxRig&>(rig_).editor()); }

private:
    BoxRig rig_;
    juce::Point<float> at_, dotAt_;
    bool deleting_;
};

} // namespace

std::unique_ptr<Surface> makeLfoCardPoint() { return std::make_unique<LfoCardSurface>(); }

std::unique_ptr<Surface> makeModCurveAddedDot() { return std::make_unique<ModCurveAddedDotSurface>(); }

std::unique_ptr<Surface> makeModCurveDeletedDot() { return std::make_unique<ModCurveAddedDotSurface>(true); }

std::vector<DrawnDot> curveEditorDots(const CurveEditorComponent& curve) {
    std::vector<DrawnDot> dots;
    for (const auto& dot : curve.getDrawnDotsForTest())
        dots.push_back({dot.pos.x, dot.pos.y, dot.alpha});
    return dots;
}

} // namespace undo_glide_test
