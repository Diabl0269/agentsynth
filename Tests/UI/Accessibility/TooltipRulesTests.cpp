// TooltipRulesTests.cpp -- the button tooltip rules (docs/development/accessibility.md#the-rule):
//
// 1. ButtonTooltip: the helper drops a labelled button's own words from its tooltip and names an icon-only one.
// 2. TooltipRules: a walk over every built-in module card and the windows the EQ and Sampler cards open fails on a
//    button that shows its words and whose tooltip starts with them, and on two buttons of one module with the same
//    name and different tooltips (one action, two wordings). The cards' actions the shape window shares (Show
//    Spectrum, Pick shape, Done) are checked against their ActionTexts.h constants here and, for the window, in
//    ShapeWindowThemeTests.cpp, which runs the same walk on the Filter and LFO shape windows (kept out of this file
//    so it does not pull the curve editor's headers).
#include "../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FX/ParametricEQModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/SamplerModule.h"
#include "TooltipRulesWalk.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapView.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/Layout/ButtonTooltip.h"
#include "UI/Layout/IconButton.h"
#include "UI/ModuleViews/EQWindow.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::buttonShowsText;
using synth::ui::buttonTooltipFor;
using synth::ui::tooltipEchoesLabel;
using namespace synth::ui::test::tooltip_rules;

// ============================================================================
// The helper.
// ============================================================================

TEST(ButtonTooltip, LabelledButtonDropsItsOwnWordsAndCapitalisesTheRest) {
    juce::TextButton button("Show spectrum");
    EXPECT_EQ(buttonTooltipFor(button, "Show spectrum: draw the output"), "Draw the output");
    EXPECT_EQ(buttonTooltipFor(button, "show SPECTRUM - draw the output"), "Draw the output");
    EXPECT_EQ(buttonTooltipFor(button, "Draw the output"), "Draw the output");
    juce::TextButton done("Done");
    EXPECT_EQ(buttonTooltipFor(done, "Done (Esc)"), "(Esc)");
}

TEST(ButtonTooltip, LabelledToggleCountsAsShowingItsText) {
    juce::ToggleButton toggle("Guides");
    EXPECT_TRUE(buttonShowsText(toggle));
    EXPECT_EQ(buttonTooltipFor(toggle, "Guides: show the lines"), "Show the lines");
}

TEST(ButtonTooltip, AWordThatOnlyStartsTheSentenceIsNotAnEcho) {
    EXPECT_FALSE(tooltipEchoesLabel("Show", "Show the map of shapes"));
    EXPECT_FALSE(tooltipEchoesLabel("Duplicate", "Duplicates the shape"));
    EXPECT_TRUE(tooltipEchoesLabel("Duplicate", "Duplicate: a copy"));
    EXPECT_TRUE(tooltipEchoesLabel("Duplicate", "Duplicate"));
    EXPECT_FALSE(tooltipEchoesLabel("", "Anything"));
}

TEST(ButtonTooltip, IconOnlyButtonIsNamedFirst) {
    synth::ui::IconButton pen("Draw", synth::theme::Glyph::Close);
    pen.setTitle("Draw pen");
    EXPECT_FALSE(buttonShowsText(pen));
    EXPECT_EQ(buttonTooltipFor(pen, "drag on the lane to draw"), "Draw pen: drag on the lane to draw");
    EXPECT_EQ(buttonTooltipFor(pen, "Draw pen: drag on the lane"), "Draw pen: drag on the lane");
    juce::TextButton noText;
    noText.setTitle("Close");
    EXPECT_EQ(buttonTooltipFor(noText, "Close the panel"), "Close the panel"); // already starts with the name
    EXPECT_EQ(buttonTooltipFor(noText, "hide it"), "Close: hide it");
}

TEST(ButtonTooltip, SetButtonTooltipKeepsTheTitleAsTheName) {
    juce::TextButton button("Pick shape");
    button.setTitle("Pick shape");
    synth::ui::setButtonTooltip(button, "Pick shape: choose a shape");
    EXPECT_EQ(button.getTooltip(), "Choose a shape");
    EXPECT_EQ(button.getTitle(), "Pick shape");
}

// ============================================================================
// The walk.
// ============================================================================

ModuleComponent* cardFor(GraphEditor& editor, juce::AudioProcessorGraph::NodeID id) {
    for (auto* card : editor.getModuleComponents())
        if (card != nullptr && card->getNodeId() == id)
            return card;
    return nullptr;
}

// The same list of card types the accessibility audit builds (AccessibilityCoverageTests.cpp, EveryModuleCard).
std::vector<juce::String> cardTypes() {
    static const juce::StringArray skipped{
        "Hosted Plugin",  "Track In",      "Track Audio", "Rec Tap", "Macro In",   "Macro Out", "Macro MIDI In",
        "Macro MIDI Out", "Channel Strip", "Master",      "Amp Env", "Filter Env", "Mod Slot"};
    std::vector<juce::String> types;
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames())
        if (!skipped.contains(type))
            types.push_back(type);
    std::sort(types.begin(), types.end());
    return types;
}

// Every built-in card, plus a Filter on the Custom type and an LFO on the Custom shape (their morph-map buttons).
// `byModule` groups each module's card and windows.
class TooltipRules : public ::testing::Test {
protected:
    void SetUp() override {
        editor_.setSize(2400, 2400);
        auto& graph = engine_.getGraph();
        int slot = 0;
        auto place = [&slot](juce::AudioProcessorGraph::Node::Ptr node) {
            node->properties.set("x", 40 + (slot % 8) * 290);
            node->properties.set("y", 40 + (slot / 8) * 310);
            ++slot;
        };
        for (const auto& type : cardTypes())
            if (auto node = graph.addNode(synth::AIStateMapper::createModule(type))) {
                place(node);
                nodes_.emplace_back(type, node->nodeID);
            }

        customFilter_ = new FilterModule();
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(customFilter_, "filterType")) =
            FilterModule::kCustomTypeIndex;
        auto filterNode = graph.addNode(std::unique_ptr<juce::AudioProcessor>(customFilter_));
        place(filterNode);
        nodes_.emplace_back("Filter", filterNode->nodeID);
        filterId_ = filterNode->nodeID;

        customLfo_ = new LFOModule();
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(customLfo_, "shape")) =
            LFOModule::kCustomShapeIndex;
        auto lfoNode = graph.addNode(std::unique_ptr<juce::AudioProcessor>(customLfo_));
        place(lfoNode);
        nodes_.emplace_back("LFO", lfoNode->nodeID);
        lfoId_ = lfoNode->nodeID;

        editor_.updateComponents();
        sizeModuleComponents(editor_);
        for (const auto& [type, id] : nodes_)
            if (auto* card = cardFor(editor_, id)) {
                card->timerCallback(); // the LFO reads its shape on the gated tick
                collectButtons(*card, "[" + type + " card]", byModule_[type]);
            }
    }

    AudioEngine engine_;
    GraphEditor editor_{engine_};
    std::vector<std::pair<juce::String, juce::AudioProcessorGraph::NodeID>> nodes_;
    std::map<juce::String, std::vector<ButtonTip>> byModule_;
    FilterModule* customFilter_ = nullptr;
    LFOModule* customLfo_ = nullptr;
    juce::AudioProcessorGraph::NodeID filterId_, lfoId_;
};

TEST_F(TooltipRules, NoLabelledButtonRepeatsItsLabelInItsTooltip) {
    auto all = std::vector<ButtonTip>();
    for (const auto& [type, buttons] : byModule_)
        append(all, buttons);
    ParametricEQModule eq;
    EQWindow eqWindow(eq);
    collectButtons(eqWindow, "[EQ window]", all);
    SamplerModule sampler;
    synth::ui::SamplerMapView map(sampler, [](const std::vector<synth::sampler::RegionSpec>&) { return true; });
    collectButtons(map, "[Sampler map]", all);

    ASSERT_GT(byModule_.size(), 30u) << "the walk must reach the built-in cards";
    const auto found = echoes(all);
    EXPECT_TRUE(found.empty()) << "a button that shows its words has a tooltip starting with them; give it only the "
                                  "explanation (synth::ui::setButtonTooltip):"
                               << listing(found);
}

// One module's card and the windows it opens: a name means one action, so it has one tooltip.
TEST_F(TooltipRules, OneModulesButtonsWithOneNameShareOneTooltip) {
    auto filter = byModule_["Filter"];
    auto lfo = byModule_["LFO"];
    auto eqButtons = byModule_["Parametric EQ"];
    ParametricEQModule eq;
    EQWindow eqWindow(eq);
    collectButtons(eqWindow, "[EQ window]", eqButtons);

    for (const auto& [module, buttons] : std::map<juce::String, std::vector<ButtonTip>>{
             {"Filter", filter}, {"LFO", lfo}, {"Parametric EQ", eqButtons}}) {
        ASSERT_FALSE(buttons.empty()) << module;
        const auto found = twinsThatDiffer(buttons);
        EXPECT_TRUE(found.empty()) << module
                                   << ": one name, different tooltips (use one actiontext constant):" << listing(found);
    }
}

// The Sampler map's Done is the shape window's Done (ShapeWindowThemeTests.cpp checks that side): one constant.
TEST_F(TooltipRules, SamplerMapDoneIsTheSharedDone) {
    SamplerModule sampler;
    synth::ui::SamplerMapView map(sampler, [](const std::vector<synth::sampler::RegionSpec>&) { return true; });
    std::vector<ButtonTip> buttons;
    collectButtons(map, "[Sampler map]", buttons);
    int dones = 0;
    for (const auto& b : buttons)
        if (b.name == synth::ui::actiontext::kDone) {
            ++dones;
            EXPECT_EQ(b.tip, synth::ui::actiontext::kDoneCloseWindowTip) << b.path;
        }
    EXPECT_EQ(dones, 1);
}

// The Filter card's Show Spectrum and Pick shape carry the shared words and tooltips the shape window also uses
// (ShapeWindowThemeTests.cpp checks the window against the same constants).
TEST_F(TooltipRules, FilterCardsSharedActionsUseTheirConstants) {
    auto* card = cardFor(editor_, filterId_);
    ASSERT_NE(card, nullptr);
    const auto& buttons = byModule_["Filter"];
    auto find = [&](const juce::String& text) -> const ButtonTip* {
        for (const auto& b : buttons)
            if (b.text == text)
                return &b;
        return nullptr;
    };
    const auto* spectrum = find(synth::ui::actiontext::kShowSpectrum);
    ASSERT_NE(spectrum, nullptr);
    EXPECT_EQ(spectrum->tip, juce::String(synth::ui::actiontext::kShowSpectrumFilterTip));
    EXPECT_TRUE(spectrum->tip.contains("before any volume stage"));
    const auto* pick = find(synth::ui::actiontext::kPickShape);
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->name, juce::String(synth::ui::actiontext::kPickShape));
    EXPECT_EQ(pick->tip, juce::String(synth::ui::actiontext::kPickShapeTip));
}

} // namespace
