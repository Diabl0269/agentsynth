// PointEditorMatrixEqRows.cpp -- the matrix row for the Parametric EQ's curve: 24 band slots, three of them on, in a
// real graph. A point is an enabled band; its position is the octave of its frequency, its value its gain.
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "PointEditorMatrixDriver.h"
#include "PointEditorMatrixHoldHost.h"
#include "UI/Layout/KeyGlide/KeyHold.h"
#include "UI/Layout/PopupHost.h"
#include "UI/ModuleViews/EQBandTypeMenu.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <algorithm>
#include <cmath>

namespace point_matrix {
namespace {

class EqCurve : public PointDriver {
public:
    EqCurve() {
        auto* eq = new ParametricEQModule();
        const float freqs[] = {200.0f, 1000.0f, 4000.0f};
        const float gains[] = {3.0f, -4.0f, 5.0f};
        for (int b = 0; b < 3; ++b) {
            eq->setBandFreq(b, freqs[b]);
            eq->setBandGain(b, gains[b]);
            eq->setBandEnabled(b, true);
        }
        module_ = eq;
        node_ = engine_.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(eq));
        curve_ = std::make_unique<EQCurveComponent>(*module_);
        curve_->setBounds(0, 0, 400, 160);
        curve_->onGestureStart = [this] { ++gestures_; };
    }
    ~EqCurve() override { curve_.reset(); }

    juce::Component& surface() override { return *curve_; }
    int points() const override { return (int)enabled().size(); }
    int selected() const override { return (int)curve_->getSelectedBands().size(); }
    int selectedIndex() const override {
        const auto bands = enabled();
        const auto it = std::find(bands.begin(), bands.end(), curve_->getSelectedBand());
        return it == bands.end() ? -1 : (int)(it - bands.begin());
    }
    bool selectedDot(int point) const override { return curve_->isBandSelected(enabled()[(size_t)point]); }
    double position(int point) const override { return std::log2(snapshot(point).freqHz); }
    double value(int point) const override { return snapshot(point).gainDb; }
    juce::Point<float> dot(int point) override { return curve_->bandPosition(snapshot(point)); }
    int editSteps() const override { return gestures_; }
    std::optional<HoldHooks> hold() override {
        auto& c = *curve_;
        HoldHooks h;
        h.install = [&c, keys = h.keys] { installHold(c, keys); };
        h.tick = [&c] { c.getKeyHoldForTest().tick(); };
        h.active = [&c] { return c.getKeyHoldForTest().active(); };
        h.keyUp = [&c] { c.keyStateChanged(false); };
        h.tapMs = synth::ui::lane_key::kTapMs;
        return h;
    }
    // Slots 0..2 become 4 kHz, 200 Hz, 1 kHz: the chips read 1, 2, 0 while the slots read 0, 1, 2.
    bool renumberPoints() override {
        const float freqs[] = {4000.0f, 200.0f, 1000.0f};
        for (int b = 0; b < 3; ++b)
            module_->setBandFreq(b, freqs[b]);
        curve_->timerCallback(); // takes the new snapshots in
        return true;
    }
    bool floatingReadoutShowing() override { return !curve_->getReadoutBands().empty(); }
    void hoverPoint(int point) override { curve_->mouseMove(pointerAt(dot(point))); }
    void leavePoint() override { curve_->mouseExit(pointerAt(dot(0))); }
    bool key(int code, int mods) override { return curve_->keyPressed(keyOf(code, mods)); }
    // An empty place high up between the first two bands.
    juce::Point<float> emptySpot() override { return {(dot(0).x + dot(1).x) * 0.5f, 2.0f}; }
    // A right-click on a band opens its type menu at the dot; the popover is captured, never shown, so no window is
    // left open for the next test.
    std::optional<juce::Rectangle<int>> rightClick(int point) override {
        std::optional<juce::Rectangle<int>> area;
        auto& launcher = synth::ui::test_hooks::popoverLauncherForTest();
        entries_.clear();
        launcher = [&](std::unique_ptr<juce::Component> content, juce::Rectangle<int> at) -> juce::CallOutBox* {
            area = at;
            if (auto* menu = dynamic_cast<synth::ui::EQBandTypeMenu*>(content.get()))
                if (auto* row = menu->deleteRowForTest())
                    entries_.push_back(row->getTitle().toStdString());
            return nullptr;
        };
        rightPress(dot(point));
        launcher = nullptr;
        return area;
    }
    std::optional<std::vector<std::string>> rightClickEntries() const override { return entries_; }

private:
    juce::MouseEvent pointerAt(juce::Point<float> p) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, curve_.get(), curve_.get(), juce::Time::getCurrentTime(), p,
                                juce::Time::getCurrentTime(), 1, false);
    }
    // The points run in frequency order, not slot order: a duplicate lands in the lowest free slot but two semitones
    // above its original, so "right after the original along the axis" is a frequency question.
    std::vector<int> enabled() const {
        std::vector<int> bands;
        for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
            if (module_->isBandEnabled(b))
                bands.push_back(b);
        const auto snapshots = module_->getBandSnapshots();
        std::stable_sort(bands.begin(), bands.end(),
                         [&](int a, int b) { return snapshots[(size_t)a].freqHz < snapshots[(size_t)b].freqHz; });
        return bands;
    }
    ParametricEQModule::BandSnapshot snapshot(int point) const {
        return module_->getBandSnapshots()[(size_t)enabled()[(size_t)point]];
    }

    AudioEngine engine_;
    juce::AudioProcessorGraph::Node::Ptr node_;
    ParametricEQModule* module_ = nullptr; // owned by the graph
    std::unique_ptr<EQCurveComponent> curve_;
    int gestures_ = 0;
    std::vector<std::string> entries_;
};

} // namespace

void addEqRows(std::vector<MatrixRow>& rows) {
    rows.push_back({"EQ curve", "EQCurveComponent", [] { return std::make_unique<EqCurve>(); }});
}

} // namespace point_matrix
