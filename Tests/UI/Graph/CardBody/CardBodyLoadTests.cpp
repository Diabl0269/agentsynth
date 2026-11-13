// CardBodyLoadTests.cpp
//
// Opening a project builds every card through its card body while the graph and macros are already in
// place (GraphEditor::updateComponents after the load). Cards with a stored layout that hides
// parameters, and macros holding LFO and ADSR members (whose constructors re-measure the card), must
// build without a crash and leave every card exactly where it was saved.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "CardBodyTestHelpers.h"
#include "MacroSet.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"

using namespace cardbody_test;

namespace {

struct Saved {
    NodeID id;
    juce::Point<int> at;
};

// The automatic layout with the module's first widget-bearing parameter hidden, or nullopt when the
// module has none (or its card is bespoke and ignores layouts).
std::optional<synth::CardLayout> hidingFirstParameter(juce::AudioProcessor& module) {
    if (!synth::cardBodyLayoutIsDataDriven(module))
        return std::nullopt;
    const auto plan = synth::CardBodyPlan::forModule(module, std::nullopt);
    for (const auto& item : plan.items)
        if (item.param != nullptr)
            return automaticLayoutHiding(module, {item.param->paramID});
    return std::nullopt;
}

Saved addSaved(CardCanvas& c, std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
    auto layout = hidingFirstParameter(*processor);
    return {c.add(std::move(processor), x, y, layout), {x, y}};
}

void addMacro(CardCanvas& c, const std::vector<Saved>& members, bool collapsed) {
    synth::Macro macro;
    macro.name = "Voice";
    macro.collapsed = collapsed;
    macro.bounds = {members.front().at.x, members.front().at.y, 160, 60};
    for (const auto& m : members)
        macro.members.push_back(c.engine.getGraph().getNodeForId(m.id)->properties["uuid"].toString());
    c.editor.getMacros().add(macro);
}

void expectOpenedAsSaved(CardCanvas& c, const std::vector<Saved>& nodes) {
    for (const auto& n : nodes) {
        auto* card = c.card(n.id);
        ASSERT_NE(card, nullptr) << "node " << (int)n.id.uid;
        EXPECT_EQ(card->getPosition(), n.at) << "node " << (int)n.id.uid << " was moved by the load";
        auto* body = card->getCardBody();
        if (body != nullptr && body->hasMoreRow()) {
            EXPECT_FALSE(body->isMoreUnfolded()) << "a loaded card opens folded";
            ASSERT_NE(body->getMoreButton(), nullptr);
            EXPECT_TRUE(card->getLocalBounds().contains(body->getMoreButton()->getBounds()));
        }
    }
}

} // namespace

TEST(CardBodyLoad, MacrosWithLfoAndAdsrMembersAndHiddenParametersOpenAsSaved) {
    for (const bool collapsed : {false, true}) {
        SCOPED_TRACE(collapsed ? "collapsed" : "open");
        CardCanvas c;
        auto lfo = addSaved(c, std::make_unique<LFOModule>(), 400, 300);
        auto adsr = addSaved(c, std::make_unique<ADSRModule>(), 700, 300);
        auto osc = addSaved(c, std::make_unique<OscillatorModule>(), 1000, 300);
        auto loose = addSaved(c, std::make_unique<FilterModule>(), 1400, 300);
        addMacro(c, {lfo, adsr, osc}, collapsed);
        c.editor.updateComponents();
        if (collapsed)
            expectOpenedAsSaved(c, {loose});
        else
            expectOpenedAsSaved(c, {lfo, adsr, osc, loose});
        if (auto* filterCard = c.card(loose.id))
            EXPECT_TRUE(filterCard->getCardBody()->hasMoreRow()) << "the stored layout was read on load";
    }
}

TEST(CardBodyLoad, EveryModuleTypeWithAHiddenParameterOpensAsSaved) {
    for (const auto& type : synth::AIStateMapper::moduleFactoryTypeNames()) {
        SCOPED_TRACE(type.toStdString());
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr || dynamic_cast<AttenuverterModule*>(processor.get()) != nullptr)
            continue; // an Attenuverter draws no card
        if (synth::isOutputDockProcessor(processor.get()))
            continue; // the output dock is placed by reflowOutputDock, never at its saved x
        CardCanvas c;
        auto neighbour = addSaved(c, std::make_unique<OscillatorModule>(), 420, 340);
        auto subject = addSaved(c, std::move(processor), 400, 300);
        auto lfo = addSaved(c, std::make_unique<LFOModule>(), 700, 300);
        addMacro(c, {subject, lfo}, /*collapsed*/ false);
        c.editor.updateComponents();
        expectOpenedAsSaved(c, {neighbour, subject, lfo});
    }
}

// Rebuilding every card (an undo/redo restore) drops the old card bodies and builds new ones from the
// node's stored layout; nothing moves and the fold state starts folded again.
TEST(CardBodyLoad, RebuildingTheCardsKeepsTheLayoutAndPositions) {
    CardCanvas c;
    auto filter = addSaved(c, std::make_unique<FilterModule>(), 400, 300);
    auto lfo = addSaved(c, std::make_unique<LFOModule>(), 800, 300);
    c.editor.updateComponents();
    c.card(filter.id)->getCardBody()->setMoreUnfolded(true);
    c.card(filter.id)->getCardBody()->setMoreUnfolded(false);
    c.editor.detachAllModuleComponents();
    c.editor.updateComponents();
    expectOpenedAsSaved(c, {filter, lfo});
    EXPECT_TRUE(c.card(filter.id)->getCardBody()->hasMoreRow());
}
