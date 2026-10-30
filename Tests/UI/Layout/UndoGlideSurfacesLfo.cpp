// UndoGlideSurfacesLfo.cpp -- the registry fixture for the LFO card's Custom-wave curve editor
// (docs/layout/undo-glide.md).
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
    juce::Component* paintedComponent() override { return curve_; }

private:
    synth::ui::CurveEditorGeometry geometry() const {
        synth::ui::CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return synth::ui::CurveEditorGeometry(curve_->getModel(), curve_->getLocalBounds().toFloat(), config);
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

} // namespace

std::unique_ptr<Surface> makeLfoCardPoint() { return std::make_unique<LfoCardSurface>(); }

} // namespace undo_glide_test
