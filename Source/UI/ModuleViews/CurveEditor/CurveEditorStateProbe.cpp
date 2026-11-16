// Concern: what test mode reads from a curve editor (see CurveEditorStateProbe.h).
#include "CurveEditorStateProbe.h"

#include "CurveEditorComponent.h"
#include "TestMode/TestModeStateSource.h"

namespace synth::ui {

namespace {
class CurveEditorStateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit CurveEditorStateProbe(const CurveEditorComponent& editor)
        : editor_(editor) {
        setComponentID("curveEditorState");
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
        setBounds(0, 0, 1, 1);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        const auto& model = editor_.getModel();
        juce::StringArray xs;
        for (int i = 0; i < model.getNumNodes(); ++i)
            xs.add(juce::String(model.getNode(i).x, 2));
        into.setProperty("points", model.getNumNodes());
        into.setProperty("selected", (int)editor_.getSelectedIndices().size());
        into.setProperty("cursor", editor_.getSelectedIndex());
        into.setProperty("xs", xs.joinIntoString(" "));
    }

private:
    const CurveEditorComponent& editor_;
};
} // namespace

std::unique_ptr<juce::Component> makeCurveEditorStateProbe(const CurveEditorComponent& editor) {
    return std::make_unique<CurveEditorStateProbe>(editor);
}

} // namespace synth::ui
