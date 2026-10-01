// CardBodyGoldenTests.cpp
//
// Every built-in card must look exactly as it did before cards were drawn from layout data. The
// golden file (CardBodyGolden.txt, next to this one) records, per module type, the card size, every
// child component in child order (kind, componentID, bounds, visibility, title, tooltip, focus order)
// and every jack centre. It was captured from the card code as it stood before CardBody, so a diff
// here is a visible change. Child order plus focus order pins Tab order; jack centres pin the
// knob-bound-jack rule.
//
// Regenerate (only for a deliberate visual change): CARDBODY_WRITE_GOLDEN=1 ./Tests
//   --gtest_filter='CardBodyGolden*'

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <gtest/gtest.h>

namespace {

juce::File goldenFile() { return juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/card-body/card-geometry.golden"); }

// Internal (library-less) types the size estimate also names, on top of the library's own list.
juce::StringArray goldenTypes() {
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra :
         {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Hosted Plugin", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);
    return types;
}

// A coarse, compiler-independent widget kind (typeid names differ between toolchains).
juce::String kindOf(juce::Component& c) {
    if (dynamic_cast<synth::ui::CardKnobSlider*>(&c) != nullptr)
        return "knob";
    if (dynamic_cast<juce::Slider*>(&c) != nullptr)
        return "slider";
    if (dynamic_cast<juce::ComboBox*>(&c) != nullptr)
        return "combo";
    if (dynamic_cast<juce::ToggleButton*>(&c) != nullptr)
        return "toggle";
    if (dynamic_cast<juce::TextButton*>(&c) != nullptr)
        return "textButton";
    if (dynamic_cast<juce::DrawableButton*>(&c) != nullptr)
        return "drawableButton";
    if (dynamic_cast<juce::Button*>(&c) != nullptr)
        return "button";
    if (dynamic_cast<juce::Label*>(&c) != nullptr)
        return "label";
    if (dynamic_cast<juce::TextEditor*>(&c) != nullptr)
        return "textEditor";
    return "component";
}

juce::String rectText(juce::Rectangle<int> r) {
    return juce::String(r.getX()) + "," + juce::String(r.getY()) + " " + juce::String(r.getWidth()) + "x" +
           juce::String(r.getHeight());
}

juce::String quoted(const juce::String& s) { return "\"" + s.replace("\n", "\\n") + "\""; }

void describeCard(const juce::String& type, ModuleComponent& comp, juce::StringArray& out) {
    out.add("type " + type + " size " + juce::String(comp.getWidth()) + "x" + juce::String(comp.getHeight()));
    int index = 0;
    for (auto* child : comp.getChildren()) {
        juce::String tooltip;
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(child))
            tooltip = client->getTooltip();
        out.add("  child " + juce::String(index++) + " " + kindOf(*child) + " id=" + quoted(child->getComponentID()) +
                " at " + rectText(child->getBounds()) + " visible=" + juce::String((int)child->isVisible()) +
                " title=" + quoted(child->getTitle()) + " tip=" + quoted(tooltip) +
                " focusOrder=" + juce::String(child->getExplicitFocusOrder()) +
                " wantsFocus=" + juce::String((int)child->getWantsKeyboardFocus()));
    }
    auto* processor = comp.getModule();
    int ins = processor->getTotalNumInputChannels();
    int outs = processor->getTotalNumOutputChannels();
    if (auto* mb = dynamic_cast<ModuleBase*>(processor)) {
        ins = mb->getVisibleInputPortCount();
        outs = mb->getVisibleOutputPortCount();
    }
    for (int i = 0; i < ins; ++i)
        out.add("  in " + juce::String(i) + " " + comp.getPortCenter(i, true).toString());
    for (int i = 0; i < outs; ++i)
        out.add("  out " + juce::String(i) + " " + comp.getPortCenter(i, false).toString());
}

juce::StringArray describeAllCards() {
    AudioEngine engine;
    GraphEditor editor(engine);
    juce::StringArray lines;
    for (const auto& type : goldenTypes()) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr) {
            lines.add("type " + type + " (no factory entry)");
            continue;
        }
        ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        describeCard(type, comp, lines);
    }
    return lines;
}

} // namespace

TEST(CardBodyGolden, EveryCardMatchesTheGeometryCapturedBeforeCardBody) {
    const auto actual = describeAllCards();

    if (juce::SystemStats::getEnvironmentVariable("CARDBODY_WRITE_GOLDEN", {}).isNotEmpty()) {
        ASSERT_TRUE(goldenFile().replaceWithText(actual.joinIntoString("\n") + "\n"));
        GTEST_SKIP() << "wrote " << goldenFile().getFullPathName();
    }

    juce::StringArray expected;
    expected.addLines(goldenFile().loadFileAsString());
    expected.removeEmptyStrings();
    ASSERT_GT(expected.size(), 0) << "missing golden file " << goldenFile().getFullPathName();

    int reported = 0;
    const int common = std::min(expected.size(), actual.size());
    for (int i = 0; i < common && reported < 25; ++i) {
        if (expected[i] != actual[i]) {
            ADD_FAILURE() << "line " << (i + 1) << "\n  expected: " << expected[i] << "\n  actual:   " << actual[i];
            ++reported;
        }
    }
    EXPECT_EQ(expected.size(), actual.size()) << "golden line count";
}
