// UndoGlideSurfacesDrumPad.cpp -- the registry fixture for the Drum Kit's pad panel: a pad knob changed by the mouse
// and restored by the real AppUndoManager glides its drawn value (the pad pick itself is not an undo step; it glides
// the same knobs, covered by DrumKitPadGlideTests). docs/layout/undo-glide.md.
#include "../Graph/CardBody/CardCanvas.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UndoGlideSurfaces.h"

namespace undo_glide_test {

namespace {

using cardbody_test::CardCanvas;
using PadParam = DrumKitModule::PadParam;

/** The picked pad's Level knob, dragged with the mouse (one undo step); the card's parameter listener restores it. */
class DrumPadKnobSurface : public Surface {
public:
    DrumPadKnobSurface() {
        id_ = canvas_.add(std::make_unique<DrumKitModule>(), 0, 0);
        canvas_.editor.updateComponents();
        card_ = canvas_.card(id_);
        panel().valueGlideForTest().forceAnimateForTest(true);
        if (panel().getWidth() < 1)
            panel().setSize(400, 200);
    }

    void move() override {
        auto& knob = panel().knob(PadParam::Level);
        const auto centre = knob.getLocalBounds().getCentre();
        const auto plain = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
        const auto at = [&](int dy) {
            return cardbody_test::mouseAt(knob, (centre + juce::Point<int>(0, dy)).toFloat(), plain);
        };
        knob.mouseDown(at(0));
        knob.mouseDrag(at(-70));
        knob.mouseUp(at(-70));
    }
    bool undo() override { return restore(false); }
    bool redo() override { return restore(true); }
    double drawn() const override { return 100.0 * panel().shownProportion(PadParam::Level); }
    bool gliding() const override { return const_cast<DrumPadKnobSurface*>(this)->panel().isMoving(); }
    void stepTo(double elapsedMs) override { panel().stepMotionForTest(elapsedMs); }
    float alpha() const override {
        return const_cast<DrumPadKnobSurface*>(this)->panel().knob(PadParam::Level).getAlpha();
    }
    juce::Component* paintedComponent() override { return &panel(); }

private:
    synth::ui::DrumPadControls& panel() const {
        return dynamic_cast<synth::ui::DrumKitChrome*>(card_->getInstrumentChrome())->getPads().controls();
    }
    bool restore(bool forward) {
        const bool did = forward ? canvas_.undo.redo() : canvas_.undo.undo();
        card_->timerCallback();
        return did;
    }

    CardCanvas canvas_;
    cardbody_test::NodeID id_;
    ModuleComponent* card_ = nullptr;
};

} // namespace

std::unique_ptr<Surface> makeDrumPadKnob() { return std::make_unique<DrumPadKnobSurface>(); }

} // namespace undo_glide_test
