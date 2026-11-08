// UndoGlideSurfacesEq.cpp -- the registry fixtures for the Parametric EQ's curves: the card's, the pop-out window's and
// the mixer thumbnail's (docs/layout/undo-glide.md). Each moves band 2 through its real edit path (a mouse drag on the
// curve, or an undo-bracketed parameter write for the thumbnail) and restores it with the real AppUndoManager.
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Mixer/MixerEqThumbnail.h"
#include "UI/ModuleViews/EQWindow.h"
#include "UndoGlideSurfaces.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace undo_glide_test {

namespace {

constexpr int kBand = 1;

juce::MouseEvent mouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged, juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

/** A module with band 2 on at 1 kHz, +6 dB, in an engine graph (so the undo snapshots see it). */
struct EqRig {
    EqRig() {
        auto* eq = new ParametricEQModule();
        eq->setBandFreq(kBand, 1000.0f);
        eq->setBandGain(kBand, 6.0f);
        eq->setBandEnabled(kBand, true);
        module = eq;
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(eq));
    }
    AudioEngine engine;
    AppUndoManager undo;
    juce::AudioProcessorGraph::Node::Ptr node;
    ParametricEQModule* module = nullptr; // owned by the graph
};

class EqCurveSurfaceBase : public Surface {
public:
    void move() override {
        const auto from = curve().getDrawnBandPosition(kBand);
        const auto to = from + juce::Point<float>(70.0f, -20.0f);
        curve().mouseDown(mouseEvent(curve(), from, false, from));
        curve().mouseDrag(mouseEvent(curve(), to, true, from));
        curve().mouseUp(mouseEvent(curve(), to, true, from));
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override {
        return const_cast<EqCurveSurfaceBase*>(this)->curve().getDrawnBandPosition(kBand).x;
    }
    bool gliding() const override {
        return const_cast<EqCurveSurfaceBase*>(this)->curve().getBandGlideForTest().isAnimating();
    }
    void stepTo(double elapsedMs) override { curve().getBandGlideForTest().stepForTest(elapsedMs); }
    float alpha() const override { return const_cast<EqCurveSurfaceBase*>(this)->curve().getDrawnBandAlpha(kBand); }
    juce::Component* paintedComponent() override { return &curve(); }

protected:
    virtual EQCurveComponent& curve() = 0;
    EqRig rig_;

private:
    bool restore(bool forward) {
        const bool did = forward ? rig_.undo.redo() : rig_.undo.undo();
        curve().timerCallback(); // the curve's 30 Hz poll picks the restored bands up
        return did;
    }
};

EQCurveComponent* findCurve(juce::Component& parent) {
    for (auto* child : parent.getChildren())
        if (auto* curve = dynamic_cast<EQCurveComponent*>(child))
            return curve;
    return nullptr;
}

class EqCardSurface : public EqCurveSurfaceBase {
public:
    EqCardSurface() {
        editor_ = std::make_unique<GraphEditor>(rig_.engine);
        card_ = std::make_unique<ModuleComponent>(rig_.module, rig_.node->nodeID, *editor_, &rig_.undo);
        curve_ = findCurve(*card_);
        if (curve_->getWidth() < 1)
            curve_->setBounds(0, 0, 300, 150);
        curve_->getBandGlideForTest().forceAnimateForTest(true);
    }

protected:
    EQCurveComponent& curve() override { return *curve_; }

private:
    std::unique_ptr<GraphEditor> editor_;
    std::unique_ptr<ModuleComponent> card_;
    EQCurveComponent* curve_ = nullptr;
};

class EqWindowSurface : public EqCurveSurfaceBase {
public:
    EqWindowSurface() {
        window_ = std::make_unique<EQWindow>(*rig_.module);
        auto* engine = &rig_.engine;
        auto* undo = &rig_.undo;
        window_->setGestureCallbacks([engine, undo] { undo->captureBeforeState(engine->getGraph()); },
                                     [engine, undo] { undo->pushSnapshotFromCapture(engine->getGraph()); });
        window_->setUndoManager(&rig_.undo);
        window_->getCurve().getBandGlideForTest().forceAnimateForTest(true);
    }

protected:
    EQCurveComponent& curve() override { return window_->getCurve(); }

private:
    std::unique_ptr<EQWindow> window_;
};

/** The mixer column's thumbnail: band 2's gain written inside an undo bracket (what the card knob or the curve does).
 */
class EqThumbnailSurface : public Surface {
public:
    EqThumbnailSurface() {
        thumbnail_.setSize(160, 36);
        thumbnail_.setEqModule(rig_.module, &rig_.engine.getGraph(), rig_.node->nodeID);
        thumbnail_.setUndoManager(&rig_.undo);
        thumbnail_.getBandGlideForTest().forceAnimateForTest(true);
    }
    ~EqThumbnailSurface() override { thumbnail_.setEqModule(nullptr); }

    void move() override {
        rig_.undo.captureBeforeState(rig_.engine.getGraph());
        rig_.module->setBandGain(kBand, 18.0f);
        rig_.undo.pushSnapshotFromCapture(rig_.engine.getGraph());
        thumbnail_.flushForTest();
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return thumbnail_.getDrawnMagnitudeDbForTest(kSampleAtBand); }
    bool gliding() const override {
        return const_cast<EqThumbnailSurface*>(this)->thumbnail_.getBandGlideForTest().isAnimating();
    }
    void stepTo(double elapsedMs) override { thumbnail_.getBandGlideForTest().stepForTest(elapsedMs); }
    float alpha() const override {
        return const_cast<EqThumbnailSurface*>(this)->thumbnail_.getBandGlideForTest().alphaFor(
            synth::ui::EqBandGlide::placeKey(kBand));
    }
    juce::Component* paintedComponent() override { return &thumbnail_; }

private:
    // The 48 samples run 20 Hz to 20 kHz on a log scale; 1 kHz is 2/3 of the way along.
    static constexpr int kSampleAtBand = 32;
    bool restore(bool forward) {
        const bool did = forward ? rig_.undo.redo() : rig_.undo.undo();
        thumbnail_.flushForTest(); // the queued parameter refresh
        return did;
    }

    EqRig rig_;
    synth::ui::MixerEqThumbnail thumbnail_;
};

} // namespace

std::unique_ptr<Surface> makeEqCardCurve() { return std::make_unique<EqCardSurface>(); }
std::unique_ptr<Surface> makeEqWindowCurve() { return std::make_unique<EqWindowSurface>(); }
std::unique_ptr<Surface> makeEqMixerThumbnail() { return std::make_unique<EqThumbnailSurface>(); }

} // namespace undo_glide_test
