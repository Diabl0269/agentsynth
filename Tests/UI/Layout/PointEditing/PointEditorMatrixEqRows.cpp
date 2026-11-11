// PointEditorMatrixEqRows.cpp -- the matrix row for the Parametric EQ's curve: four band slots, three of them on, in a
// real graph. A point is an enabled band; its position is the octave of its frequency, its value its gain.
#include "AudioEngine/AudioEngine.h"
#include "Modules/FX/ParametricEQModule.h"
#include "PointEditorMatrixDriver.h"
#include "UI/ModuleViews/EQCurveComponent.h"
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
    int selected() const override { return curve_->getSelectedBand() >= 0 ? 1 : 0; }
    int selectedIndex() const override {
        const auto bands = enabled();
        const auto it = std::find(bands.begin(), bands.end(), curve_->getSelectedBand());
        return it == bands.end() ? -1 : (int)(it - bands.begin());
    }
    bool selectedDot(int point) const override { return curve_->getSelectedBand() == enabled()[(size_t)point]; }
    double position(int point) const override { return std::log2(snapshot(point).freqHz); }
    double value(int point) const override { return snapshot(point).gainDb; }
    juce::Point<float> dot(int point) override { return curve_->bandPosition(snapshot(point)); }
    int editSteps() const override { return gestures_; }
    bool key(int code, int mods) override { return curve_->keyPressed(keyOf(code, mods)); }
    // An empty place high up between the first two bands.
    juce::Point<float> emptySpot() override { return {(dot(0).x + dot(1).x) * 0.5f, 2.0f}; }
    // The curve has no menu: a right-click is a plain press on the dot.
    std::optional<juce::Rectangle<int>> rightClick(int point) override {
        rightPress(dot(point));
        return std::nullopt;
    }

private:
    std::vector<int> enabled() const {
        std::vector<int> bands;
        for (int b = 0; b < ParametricEQModule::kShownBands; ++b)
            if (module_->isBandEnabled(b))
                bands.push_back(b);
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
};

} // namespace

void addEqRows(std::vector<MatrixRow>& rows) {
    rows.push_back({"EQ curve", "EQCurveComponent", [] { return std::make_unique<EqCurve>(); }});
}

} // namespace point_matrix
