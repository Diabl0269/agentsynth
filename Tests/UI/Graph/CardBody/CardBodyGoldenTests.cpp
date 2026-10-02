// CardBodyGoldenTests.cpp
//
// Every built-in card must look exactly as its golden file says. One file per module type,
// Tests/fixtures/card-body/<Type>.golden (the type name with every run of characters other than
// letters and digits made one "-"), records the card size, every child component in child order
// (kind, componentID, bounds, visibility, title, tooltip, focus order) and every jack centre. They
// were captured from the card code as it stood before CardBody, so a diff here is a visible change.
// Child order plus focus order pins Tab order; jack centres pin the knob-bound-jack rule.
//
// Regenerate (only for a deliberate visual change): CARDBODY_WRITE_GOLDEN=1 rewrites every file;
// CARDBODY_WRITE_GOLDEN=LFO,Sample & Hold rewrites only the listed types, so work on one module
// family never touches another family's files.
//   CARDBODY_WRITE_GOLDEN=LFO ./Tests --gtest_filter='CardBodyGolden*'

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/CardKnobSlider.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <gtest/gtest.h>

namespace {

// The type name made file-name safe: every run of characters other than ASCII letters and digits
// becomes one "-", none at either end ("Sample & Hold" -> "Sample-Hold").
juce::String goldenFileStem(const juce::String& type) {
    juce::String stem;
    bool pendingDash = false;
    for (auto c : type) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!keep) {
            pendingDash = stem.isNotEmpty();
            continue;
        }
        if (pendingDash)
            stem << "-";
        pendingDash = false;
        stem << juce::String::charToString(c);
    }
    return stem;
}

// CARDBODY_WRITE_GOLDEN: empty = compare only; "1" = rewrite every type; otherwise a comma-separated
// list of type names, each rewritten (exact names, surrounding spaces ignored).
bool goldenWriteRequested(const juce::String& request, const juce::String& type) {
    if (request.trim().isEmpty())
        return false;
    if (request.trim() == "1")
        return true;
    juce::StringArray types;
    types.addTokens(request, ",", "");
    types.trim();
    return types.contains(type);
}

juce::File goldenDirectory() { return juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/card-body"); }

juce::File goldenFileFor(const juce::String& type) {
    return goldenDirectory().getChildFile(goldenFileStem(type) + ".golden");
}

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

juce::StringArray describeCardOfType(GraphEditor& editor, const juce::String& type) {
    juce::StringArray lines;
    auto processor = synth::AIStateMapper::createModule(type);
    if (processor == nullptr) {
        lines.add("type " + type + " (no factory entry)");
        return lines;
    }
    ModuleComponent comp(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
    describeCard(type, comp, lines);
    return lines;
}

// Up to 25 differing lines of one type's card, each naming the type.
void expectMatchesGolden(const juce::String& type, const juce::StringArray& actual) {
    const auto file = goldenFileFor(type);
    juce::StringArray expected;
    expected.addLines(file.loadFileAsString());
    expected.removeEmptyStrings();
    ASSERT_GT(expected.size(), 0) << "missing golden file " << file.getFullPathName();

    int reported = 0;
    const int common = std::min(expected.size(), actual.size());
    for (int i = 0; i < common && reported < 25; ++i) {
        if (expected[i] != actual[i]) {
            ADD_FAILURE() << type << " line " << (i + 1) << "\n  expected: " << expected[i]
                          << "\n  actual:   " << actual[i];
            ++reported;
        }
    }
    EXPECT_EQ(expected.size(), actual.size()) << type << " golden line count";
}

} // namespace

TEST(CardBodyGolden, EveryCardMatchesItsGoldenFile) {
    const auto request = juce::SystemStats::getEnvironmentVariable("CARDBODY_WRITE_GOLDEN", {});
    AudioEngine engine;
    GraphEditor editor(engine);
    int written = 0;
    for (const auto& type : goldenTypes()) {
        const auto actual = describeCardOfType(editor, type);
        if (goldenWriteRequested(request, type)) {
            ASSERT_TRUE(goldenFileFor(type).replaceWithText(actual.joinIntoString("\n") + "\n", false, false, "\n"));
            ++written;
            continue;
        }
        expectMatchesGolden(type, actual);
    }
    if (written > 0)
        GTEST_SKIP() << "wrote " << written << " golden file(s) in " << goldenDirectory().getFullPathName();
}

// Each type has its own file, so two module families can be recaptured on separate branches without
// touching the same file; no two types may share a file name, and no stray file may linger.
TEST(CardBodyGolden, EveryTypeHasItsOwnGoldenFileAndNoneIsStray) {
    juce::StringArray stems;
    for (const auto& type : goldenTypes()) {
        const auto stem = goldenFileStem(type);
        EXPECT_FALSE(stems.contains(stem)) << type << " shares the file name " << stem;
        stems.add(stem);
        EXPECT_TRUE(goldenFileFor(type).existsAsFile()) << type;
    }
    for (const auto& file : goldenDirectory().findChildFiles(juce::File::findFiles, false, "*.golden"))
        EXPECT_TRUE(stems.contains(file.getFileNameWithoutExtension())) << "stray " << file.getFileName();
}

TEST(CardBodyGolden, TheWriterRewritesAllTypesOrOnlyTheListedOnes) {
    EXPECT_FALSE(goldenWriteRequested("", "LFO"));
    EXPECT_TRUE(goldenWriteRequested("1", "LFO"));
    EXPECT_TRUE(goldenWriteRequested("1", "Sample & Hold"));
    EXPECT_TRUE(goldenWriteRequested("LFO,Sample & Hold", "Sample & Hold"));
    EXPECT_TRUE(goldenWriteRequested("LFO, Sample & Hold", "LFO"));
    EXPECT_FALSE(goldenWriteRequested("LFO,Sample & Hold", "Oscillator"));
    EXPECT_FALSE(goldenWriteRequested("LFO", "LFO Extra"));
    EXPECT_EQ(goldenFileStem("Sample & Hold"), "Sample-Hold");
    EXPECT_EQ(goldenFileStem("Poly MIDI"), "Poly-MIDI");
    EXPECT_EQ(goldenFileStem("LFO"), "LFO");
}
