#pragma once

// Shared rig for the curve box tests: the mod dot fixture's LFO -> attenuverter -> VCA routing, a curve box on that
// attenuverter with a preset store in a folder of its own, and mouse events for the curve editor. Header-only.

#include "../../../UniqueTempDir.h"
#include "../../Graph/ModDot/ModDotTestFixture.h"
#include "../CurveEditor/CurveEditorTestHelpers.h"
#include "UI/ModuleViews/ModCurve/ModCurveBox.h"
#include "UI/ModuleViews/ModCurve/ModCurvePopover.h"

namespace {

struct TempFolder {
    juce::File dir = synth::test::makeUniqueTempDir("modcurve");
    ~TempFolder() { dir.deleteRecursively(); }
};

struct BoxRig {
    Fixture f;
    TempFolder folder;
    std::shared_ptr<synth::ui::ModCurveEditTarget> target;
    std::unique_ptr<synth::ui::ModCurveBox> box;

    BoxRig()
        : target(std::make_shared<synth::ui::ModCurveEditTarget>(*f.editor, f.attenId))
        , box(std::make_unique<synth::ui::ModCurveBox>(target, synth::ui::ModCurvePresetStore(folder.dir))) {}
    ~BoxRig() { box.reset(); }

    synth::ModCurve curve() const { return target->read().value(); }
    synth::ui::CurveEditorComponent& editor() { return box->view().editor(); }
    synth::ui::CurveEditorGeometry geometry() {
        return synth::ui::CurveEditorGeometry(editor().getModel(), editor().getLocalBounds().toFloat());
    }
    // The point on the drawn line at model x.
    juce::Point<float> onLine(double x) {
        const auto g = geometry();
        return g.playheadPosition(editor().getModel().playheadForX(x));
    }
    void pressDragRelease(juce::Point<float> from, juce::Point<float> to) {
        auto& ed = editor();
        ed.mouseDown(synth::ui::test::curveLeftClick(ed, from));
        ed.mouseDrag(synth::ui::test::curveLeftDrag(ed, to, from));
        ed.mouseUp(synth::ui::test::curveLeftClick(ed, to));
    }
};

} // namespace
