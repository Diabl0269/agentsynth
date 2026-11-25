// CardBodyOverlapTests.cpp
//
// The guard for the layout-overlap bug kind: whatever a card shows, no two of its visible parts overlap and every
// block under the controls (any visible part that is not a control: panels, the morph card, the LFO Custom
// toolbar and curve editor) starts at least kPanelGap below the controls above it, never touching. Every module type's
// card is built in each chrome state its type has (panels, spectrum, morph map, the Custom filter type and LFO shape,
// the envelope graph) and checked once its fades have settled (off screen every change lands before the call returns).
// A card's width is fixed by its type, so there is no narrower width to try. Regression test for FRO1060: the Edit
// shapes button touched the Morph knob labels with Show Response off. Regression test for FRO1335: the LFO Custom
// Grid/Shapes/Tools row touched the Morph value boxes. Regression test for FRO1211: a horizontal fader at its
// maximum drew its cap over the value text.

#include "../../../TestWait.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "CardBodyTestHelpers.h"
#include "Modules/CardLayout.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/CardBody/ModuleCardLayoutResolver.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardIconToggle.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Layout/LayoutUtil.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/EQCurveComponent.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ScopeComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <typeinfo>

namespace {

struct ChromeState {
    bool scope = false;
    bool response = false;
    bool spectrum = false;
    std::optional<bool> morphMap;
    std::optional<bool> curveView; ///< the Filter's Custom curve view: default, hidden or shown
    bool custom = false;
    bool envelopeGraph = false;

    juce::String describe() const {
        return juce::String("scope=") + (scope ? "1" : "0") + " response=" + (response ? "1" : "0") +
               " spectrum=" + (spectrum ? "1" : "0") + " morphMap=" + (morphMap ? (*morphMap ? "1" : "0") : "default") +
               " curveView=" + (curveView ? (*curveView ? "1" : "0") : "default") + " custom=" + (custom ? "1" : "0") +
               " envelopeGraph=" + (envelopeGraph ? "1" : "0");
    }
};

std::vector<ChromeState> chromeStates() {
    std::vector<ChromeState> states;
    for (const bool custom : {false, true})
        for (const bool response : {false, true})
            for (const bool scope : {false, true})
                for (const bool envelopeGraph : {false, true})
                    for (const auto morph :
                         {std::optional<bool>(), std::optional<bool>(false), std::optional<bool>(true)})
                        for (const auto curve :
                             {std::optional<bool>(), std::optional<bool>(false), std::optional<bool>(true)}) {
                            ChromeState s;
                            s.curveView = curve;
                            s.custom = custom;
                            s.response = response;
                            s.spectrum = response;
                            s.scope = scope;
                            s.envelopeGraph = envelopeGraph;
                            s.morphMap = morph;
                            states.push_back(s);
                        }
    return states;
}

void setChoice(ModuleBase& module, const juce::String& paramId, const juce::String& choice) {
    for (auto* p : module.getParameters())
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p); c != nullptr && c->paramID == paramId)
            *c = c->choices.indexOf(choice);
}

juce::String name(const juce::Component& c) {
    return juce::String(typeid(c).name()) + " \"" + c.getComponentID() + "\" " + c.getTitle() + " " +
           c.getBounds().toString();
}

// The parts the card body itself owns: the widget of each parameter and its non-chrome views (the controls
// block a panel must stay clear of), plus any knob, fader, combo box or caption the card adds around them. The
// LFO's snap-grid box belongs to the Custom-wave toolbar under the controls, not to them.
std::set<const juce::Component*> bodyParts(ModuleComponent& card) {
    std::set<const juce::Component*> parts;
    auto* body = card.getCardBody();
    if (body == nullptr)
        return parts;
    if (auto* processor = card.getModule())
        for (auto* p : processor->getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                if (auto* widget = body->findWidget(withId->paramID))
                    parts.insert(widget);
    for (const auto view : {synth::CardView::Envelope, synth::CardView::LfoShape, synth::CardView::LfoCurve,
                            synth::CardView::Waveform, synth::CardView::Wavetable, synth::CardView::EqCurve,
                            synth::CardView::Threshold, synth::CardView::GainReduction, synth::CardView::StepGrid})
        if (auto* widget = body->findView(view))
            parts.insert(widget);
    return parts;
}

bool isBodyControl(const juce::Component& c, const std::set<const juce::Component*>& parts) {
    if (parts.count(&c) != 0)
        return true;
    if (dynamic_cast<const synth::ui::NoteLengthBox*>(&c) != nullptr)
        return false;
    return dynamic_cast<const juce::Slider*>(&c) != nullptr || dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<const juce::Label*>(&c) != nullptr;
}

// The More row, the instrument chrome (Drum Kit pads, the MIDI keyboard) and the footer pills are rows of their own
// with spacing rules of their own.
bool hasOwnSpacing(const juce::Component& c) {
    const juce::String type(typeid(c).name());
    return dynamic_cast<const juce::ToggleButton*>(&c) != nullptr ||
           dynamic_cast<const juce::MidiKeyboardComponent*>(&c) || type.contains("CardMoreButton") ||
           type.contains("DrumKit") || c.getComponentID().startsWithIgnoreCase("drum");
}

// A block is structural: any visible part that is not part of the controls above it (panels, the morph card, the
// LFO toolbar and curve editor, the Filter's curve view).
bool isBlock(const juce::Component& c, const std::set<const juce::Component*>& parts) {
    // A rule (a row's divider, a lane's title hairline) is drawn against its own header or cells, not a block.
    if (juce::String(typeid(c).name()).contains("LaneHairline"))
        return false;
    return !isBodyControl(c, parts) && !hasOwnSpacing(c);
}

// The stacked blocks of a Custom card: the button rows (Edit shapes and Pick shape; Grid, Shapes and Tools), the
// curve editors, the morph map and the response view.
bool isStackedBlock(const juce::Component& c, const std::set<const juce::Component*>& parts) {
    if (dynamic_cast<const juce::TextButton*>(&c) != nullptr ||
        dynamic_cast<const synth::ui::CurveEditorComponent*>(&c) || dynamic_cast<const synth::ui::MorphMap*>(&c) ||
        dynamic_cast<const FrequencyResponseComponent*>(&c))
        return true;
    return dynamic_cast<const juce::ComboBox*>(&c) != nullptr && parts.count(&c) == 0 &&
           dynamic_cast<const synth::ui::NoteLengthBox*>(&c) == nullptr;
}

// Rows of those blocks (parts sharing a top edge are one row): the gap between one row and the next below it that
// overlaps it sideways is kBlockGap, touching or a different size counts as a failure.
void checkBlockGaps(const std::vector<juce::Component*>& visible, const std::set<const juce::Component*>& parts,
                    const juce::String& context) {
    std::vector<juce::Component*> blocks;
    for (auto* c : visible)
        if (isStackedBlock(*c, parts))
            blocks.push_back(c);
    std::sort(blocks.begin(), blocks.end(), [](auto* a, auto* b) { return a->getY() < b->getY(); });
    std::vector<juce::Rectangle<int>> rows;
    int rowTop = -1;
    for (auto* c : blocks) {
        if (c->getY() != rowTop) {
            rows.push_back(c->getBounds());
            rowTop = c->getY();
        } else {
            rows.back() = rows.back().getUnion(c->getBounds());
        }
    }
    // Two rows with the card's controls (the lane grid) between them are not neighbours: the picture on top and the
    // buttons under the body.
    const auto bodyBetween = [&](const juce::Rectangle<int>& upper, const juce::Rectangle<int>& lower) {
        for (auto* c : visible)
            if (isBodyControl(*c, parts) && c->getY() >= upper.getBottom() && c->getBottom() <= lower.getY())
                return true;
        return false;
    };
    for (size_t i = 1; i < rows.size(); ++i)
        if (!bodyBetween(rows[i - 1], rows[i]))
            EXPECT_EQ(rows[i].getY() - rows[i - 1].getBottom(), synth::cardbody::kBlockGap)
                << context << "\n  between " << rows[i - 1].toString() << " and " << rows[i].toString();
}

// The first row of the Drum Kit's pads area (its title, Show all chip and count stepper) is a row of its own, but it
// still keeps the block gap from the controls above it: a near-touch (a gap under kBlockGap, down to zero) between
// sibling controls fails here, not only an overlap.
bool isPadsRow(const juce::Component& c) {
    return c.getComponentID() == "drumPadCount" || c.getComponentID() == "drumPadsShowAll" ||
           c.getName() == "Pads label";
}

void checkNearTouch(const std::vector<juce::Component*>& visible, const std::set<const juce::Component*>& parts,
                    const juce::String& context) {
    for (auto* row : visible) {
        if (!isPadsRow(*row))
            continue;
        for (auto* other : visible) {
            const bool above = other != row && !isPadsRow(*other) && isBodyControl(*other, parts) &&
                               other->getBottom() <= row->getY() && other->getRight() > row->getX() &&
                               other->getX() < row->getRight();
            if (above)
                EXPECT_GE(row->getY() - other->getBottom(), synth::cardbody::kBlockGap)
                    << context << "\n  " << name(*row) << "\n  nearly touches " << name(*other);
        }
    }
}

void checkCard(ModuleComponent& card, const juce::String& context, bool custom = false) {
    std::vector<juce::Component*> visible;
    for (auto* child : card.getChildren())
        if (child->isVisible() && !child->getBounds().isEmpty() &&
            !juce::String(typeid(*child).name()).contains("PortAccessible")) // jacks sit on the card edge by design
            visible.push_back(child);

    // The Filter's and the EQ's icon toggles sit on the corner of their picture by design.
    const auto isPicture = [](const juce::Component& c) {
        return dynamic_cast<const FrequencyResponseComponent*>(&c) != nullptr ||
               dynamic_cast<const EQCurveComponent*>(&c) != nullptr;
    };
    const auto onPicture = [&isPicture](const juce::Component& a, const juce::Component& b) {
        return (dynamic_cast<const synth::ui::CardIconToggle*>(&a) != nullptr && isPicture(b)) ||
               (dynamic_cast<const synth::ui::CardIconToggle*>(&b) != nullptr && isPicture(a));
    };
    for (size_t i = 0; i < visible.size(); ++i)
        for (size_t j = i + 1; j < visible.size(); ++j)
            if (!onPicture(*visible[i], *visible[j]))
                EXPECT_FALSE(visible[i]->getBounds().intersects(visible[j]->getBounds()))
                    << context << "\n  " << name(*visible[i]) << "\n  overlaps " << name(*visible[j]);

    const auto parts = bodyParts(card);
    checkNearTouch(visible, parts, context);
    if (custom)
        checkBlockGaps(visible, parts, context);
    for (auto* block : visible) {
        if (!isBlock(*block, parts))
            continue;
        for (auto* other : visible) {
            const bool above = other != block && isBodyControl(*other, parts) && other->getBottom() <= block->getY() &&
                               other->getRight() > block->getX() && other->getX() < block->getRight();
            if (above)
                EXPECT_GE(block->getY(), other->getBottom() + synth::cardbody::kPanelGap)
                    << context << "\n  " << name(*block) << "\n  is too close to " << name(*other);
        }
    }
}

// A knob, fader or combo box with a caption or control under it in the same column: the gap is at least kBlockGap, so
// the next cell's title never touches the value box above it. A section header is a row of its own.
void checkCaptionsClearOfControlsAbove(ModuleComponent& card, const juce::String& context) {
    std::vector<juce::Component*> visible;
    for (auto* child : card.getChildren())
        if (child->isVisible() && !child->getBounds().isEmpty() &&
            (dynamic_cast<juce::Slider*>(child) != nullptr || dynamic_cast<juce::Label*>(child) != nullptr ||
             dynamic_cast<juce::ComboBox*>(child) != nullptr))
            visible.push_back(child);
    for (auto* lower : visible) {
        if (lower->getComponentID() == "cardSectionHeader")
            continue;
        for (auto* upper : visible) {
            const bool above = upper != lower && dynamic_cast<juce::Label*>(upper) == nullptr &&
                               upper->getBottom() <= lower->getY() && upper->getRight() > lower->getX() &&
                               upper->getX() < lower->getRight();
            if (above)
                EXPECT_GE(lower->getY() - upper->getBottom(), synth::cardbody::kBlockGap)
                    << context << "\n  " << name(*lower) << "\n  is too close to " << name(*upper);
        }
    }
}

// The More button is a chrome row: whatever sits under it (the unfolded controls and their captions) keeps kBlockGap
// from it. Regression test for FRO2258: the open controls started 2 px under the button.
void checkMoreButtonGap(ModuleComponent& card, const juce::String& context) {
    auto* more = card.getCardBody() != nullptr ? card.getCardBody()->getMoreButton() : nullptr;
    if (more == nullptr || !more->isVisible())
        return;
    for (auto* child : card.getChildren()) {
        if (child == more || !child->isVisible() || child->getBounds().isEmpty() || child->getY() < more->getBottom() ||
            child->getRight() <= more->getX() || child->getX() >= more->getRight() ||
            dynamic_cast<juce::TextButton*>(child) != nullptr)
            continue;
        EXPECT_GE(child->getY() - more->getBottom(), synth::cardbody::kBlockGap)
            << context << "\n  " << name(*child) << "\n  is too close under the More button";
    }
}

// The More button's top edge clears whatever the card shows above it (the controls' value boxes, the footer pills,
// the panels) by kMoreRowGap: touching, or the button sitting on top of a part, fails.
void checkGapAboveMoreButton(ModuleComponent& card, const juce::String& context) {
    auto* more = card.getCardBody() != nullptr ? card.getCardBody()->getMoreButton() : nullptr;
    if (more == nullptr || !more->isVisible())
        return;
    for (auto* child : card.getChildren()) {
        if (child == more || !child->isVisible() || child->getBounds().isEmpty() || child->getY() >= more->getY() ||
            child->getRight() <= more->getX() || child->getX() >= more->getRight() ||
            juce::String(typeid(*child).name()).contains("PortAccessible"))
            continue;
        EXPECT_GE(more->getY() - child->getBottom(), synth::cardbody::kMoreRowGap)
            << context << "\n  " << name(*child) << "\n  is too close above the More button";
    }
}

// Every state a card can be put in by one parameter: its default, each toggle flipped, each choice at each value.
// Calls `visit(description, processor)` with a fresh module per state.
void forEachSingleParameterState(
    const juce::String& type,
    const std::function<void(const juce::String&, std::unique_ptr<juce::AudioProcessor>&)>& visit) {
    const auto fresh = [&type] { return synth::AIStateMapper::createModule(type); };
    auto base = fresh();
    auto* baseModule = dynamic_cast<ModuleBase*>(base.get());
    if (baseModule == nullptr)
        return;
    visit("default", base);
    const auto& params = baseModule->getParameters();
    for (int i = 0; i < params.size(); ++i) {
        if (auto* toggle = dynamic_cast<juce::AudioParameterBool*>(params[i])) {
            auto processor = fresh();
            auto* module = dynamic_cast<ModuleBase*>(processor.get());
            auto* changed = dynamic_cast<juce::AudioParameterBool*>(module->getParameters()[i]);
            *changed = !toggle->get();
            visit(toggle->paramID + "=" + (changed->get() ? "1" : "0"), processor);
        } else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(params[i])) {
            for (int value = 0; value < choice->choices.size(); ++value) {
                if (value == choice->getIndex())
                    continue;
                auto processor = fresh();
                auto* module = dynamic_cast<ModuleBase*>(processor.get());
                *dynamic_cast<juce::AudioParameterChoice*>(module->getParameters()[i]) = value;
                visit(choice->paramID + "=" + choice->choices[value], processor);
            }
        }
    }
}

} // namespace

TEST(CardBodyOverlap, NoCardPartsOverlapAndBlocksUnderTheControlsKeepTheGap) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);

    for (const auto& type : types) {
        for (const auto& state : chromeStates()) {
            // The LFO's Rate row differs between Sync and Free, so its Custom state is checked in both.
            for (const bool free : {false, true}) {
                if (free && !(state.custom && type == "LFO"))
                    continue;
                auto processor = synth::AIStateMapper::createModule(type);
                auto* module = dynamic_cast<ModuleBase*>(processor.get());
                if (module == nullptr)
                    continue;
                if (state.custom) {
                    if (type == "Filter")
                        setChoice(*module, "filterType", "Custom");
                    else if (type == "LFO")
                        setChoice(*module, "shape", "Custom");
                    else
                        continue; // only the two Custom types have a Custom state
                }
                synth::CardViewState view;
                view.showScope = state.scope;
                view.showResponse = state.response;
                if (state.response)
                    view.showSpectrum = state.spectrum;
                view.showMorphMap = state.morphMap;
                view.showCurveView = state.curveView;
                module->setCardViewState(view);
                if (free)
                    for (auto* p : module->getParameters())
                        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(p); b != nullptr && b->paramID == "mode")
                            *b = false;

                ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
                for (auto* child : card.getChildren())
                    if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
                        toggle != nullptr && toggle->getButtonText() == synth::cardbody::kShowEnvelopeText)
                        toggle->setToggleState(state.envelopeGraph, juce::sendNotificationSync);
                checkCard(card, type + ": " + state.describe() + (free ? " free" : ""), state.custom);
            }
        }
    }
}

// The Oscillator's parameter-driven states, which the chrome states above never reach: Mono and Poly, Unison 1 and 7,
// the rate on Follow notes and Fixed, each with the More row folded and unfolded (the unfolded row holds Glide Mode,
// Glide Curve and Legato). No part overlaps another, and nothing under a body control, a caption included, comes
// closer than kBlockGap to it. Regression test for FRO1955: the Glide Mode title touched the Width value box.
TEST(CardBodyOverlap, TheOscillatorIsClearInEveryParameterDrivenState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    int checked = 0;
    for (const bool poly : {false, true})
        for (const int unison : {1, 7})
            for (const char* rate : {"Follow notes", "Fixed"})
                for (const bool unfolded : {false, true}) {
                    auto processor = synth::AIStateMapper::createModule("Oscillator");
                    auto* module = dynamic_cast<ModuleBase*>(processor.get());
                    ASSERT_NE(module, nullptr);
                    for (auto* p : module->getParameters()) {
                        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
                        if (ranged == nullptr)
                            continue;
                        if (ranged->paramID == "poly")
                            ranged->setValueNotifyingHost(poly ? 1.0f : 0.0f);
                        else if (ranged->paramID == "unison")
                            ranged->setValueNotifyingHost(ranged->convertTo0to1((float)unison));
                    }
                    setChoice(*module, "rateMode", rate);
                    ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
                    if (unfolded)
                        card.getCardBody()->setMoreUnfolded(true);
                    const juce::String context = juce::String("Oscillator: ") + (poly ? "poly" : "mono") +
                                                 " unison=" + juce::String(unison) + " rate=" + rate +
                                                 (unfolded ? " more=open" : " more=folded");
                    checkCard(card, context);
                    checkCaptionsClearOfControlsAbove(card, context);
                    checkMoreButtonGap(card, context);
                    ++checked;
                }
    EXPECT_EQ(checked, 16);
}

// Every built-in card with a More row, open: the controls under the button keep the block gap from it, and the open
// card has no overlapping parts. The Oscillator was the one reported; this sweeps the rest.
TEST(CardBodyOverlap, EveryCardWithAMoreRowKeepsTheBlockGapUnderTheButtonWhenOpen) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleLibraryComponent library;
    int withMoreRow = 0;
    for (const auto& type : library.getDraggableModuleNames()) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (dynamic_cast<ModuleBase*>(processor.get()) == nullptr)
            continue;
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        auto* body = card.getCardBody();
        if (body == nullptr || body->getMoreButton() == nullptr)
            continue;
        body->setMoreUnfolded(true);
        checkMoreButtonGap(card, type + ": more=open");
        ++withMoreRow;
    }
    EXPECT_GT(withMoreRow, 0);
}

// Every card with a More row, in every state one parameter can put it in (each toggle, such as the ADSR's Sync or the
// Filter's Poly, and each choice value), folded: the button keeps kMoreRowGap from the body above it. The ADSR with
// Sync on is one of the states, and is required to have been checked.
// Regression test for FRO2325: the ADSR card with Sync on had its More button touching the fader value boxes.
TEST(CardBodyOverlap, EveryCardWithAMoreRowKeepsTheGapAboveTheButtonInEveryState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);

    std::set<juce::String> checked;
    bool adsrSyncOnChecked = false;
    for (const auto& type : types)
        forEachSingleParameterState(type, [&](const juce::String& state,
                                              std::unique_ptr<juce::AudioProcessor>& processor) {
            ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
            auto* more = card.getCardBody() != nullptr ? card.getCardBody()->getMoreButton() : nullptr;
            if (more == nullptr)
                return;
            checkGapAboveMoreButton(card, type + ": " + state);
            checked.insert(type + ": " + state);
            if (auto* sync = dynamic_cast<juce::AudioParameterBool*>(findParameterByID(processor.get(), "tempoSync"));
                type == "ADSR" && sync != nullptr && sync->get())
                adsrSyncOnChecked = true;
        });
    EXPECT_GT(checked.size(), 20u);
    EXPECT_TRUE(adsrSyncOnChecked) << "the ADSR with Sync on has a More row and is swept";
}

// The Drum Kit's pads area at the smallest and the largest pad count, with nothing hidden, some pads hidden and
// Show all pressed (the chip showing in both of its states): no part overlaps another and the Pads row keeps the gap
// from the Level box above it. Regression test for FRO1678: the Pads row touched the Level value box.
TEST(CardBodyOverlap, TheDrumKitPadsAreaIsClearAtEveryPadCountAndPadState) {
    AudioEngine engine;
    GraphEditor editor(engine);
    for (const int count : {4, 32}) {
        for (const int state : {0, 1, 2}) { // nothing hidden, two hidden, Show all pressed
            auto processor = synth::AIStateMapper::createModule("Drum Kit");
            auto* kit = dynamic_cast<DrumKitModule*>(processor.get());
            ASSERT_NE(kit, nullptr);
            kit->setPadCount(count);
            ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
            auto* chrome = dynamic_cast<synth::ui::DrumKitChrome*>(card.getInstrumentChrome());
            ASSERT_NE(chrome, nullptr);
            auto& pads = chrome->getPads();
            if (state >= 1) {
                pads.setPadShown(1, false);
                pads.setPadShown(2, false);
            }
            if (state == 2)
                pads.toggleShowAll();
            const juce::String context = "Drum Kit: " + juce::String(count) + " pads, state " + juce::String(state);
            EXPECT_EQ(pads.showAllChip().isVisible(), state >= 1) << context;
            checkCard(card, context);
        }
    }
}

namespace {

void collectFaders(juce::Component& root, std::vector<synth::ui::CardFader*>& out) {
    for (auto* child : root.getChildren()) {
        if (auto* fader = dynamic_cast<synth::ui::CardFader*>(child); fader != nullptr && !fader->isVerticalFader())
            out.push_back(fader);
        collectFaders(*child, out);
    }
}

juce::Label* valueBox(juce::Slider& slider) {
    for (auto* child : slider.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child))
            return label;
    return nullptr;
}

// How far the focus ring reaches past the cap (its gap and its stroke), and the clear space the ring keeps from the
// value box beside it.
constexpr float kFocusRingReach = 4.0f;
constexpr float kRingToBoxGap = 2.0f;

float textWidthOf(const juce::Label& label) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(label.getFont(), label.getText(), 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

} // namespace

// The painted cap and the value text box of every horizontal card fader stay apart at the lowest, default and
// highest value (with its focus ring), at the width the card lays it out at and at a narrower one; the text also fits
// its box.
TEST(CardBodyOverlap, AHorizontalFadersCapNeverTouchesItsValueText) {
    // The app's look-and-feel is the default for every card, as in the running app (the Tests binary's own default
    // is a plain JUCE one with a different thumb radius).
    synth::theme::AppLookAndFeel appLookAndFeel;
    struct DefaultLookAndFeelScope {
        explicit DefaultLookAndFeelScope(juce::LookAndFeel& l)
            : previous(&juce::LookAndFeel::getDefaultLookAndFeel()) {
            juce::LookAndFeel::setDefaultLookAndFeel(&l);
        }
        ~DefaultLookAndFeelScope() { juce::LookAndFeel::setDefaultLookAndFeel(previous); }
        juce::LookAndFeel* previous;
    } defaultLookAndFeel(appLookAndFeel);
    AudioEngine engine;
    GraphEditor editor(engine);
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Track In", "Rec Tap", "Track Audio", "Channel Strip", "Master", "Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);

    int checked = 0;
    const auto sweep = [&checked](ModuleComponent& card, const juce::String& type) {
        std::vector<synth::ui::CardFader*> faders;
        collectFaders(card, faders);
        for (auto* fader : faders) {
            auto* box = valueBox(*fader);
            ASSERT_NE(box, nullptr) << type;
            const int laidOutWidth = fader->getWidth();
            const double original = fader->getValue();
            for (const int width : {laidOutWidth, juce::jmin(laidOutWidth, 100)}) {
                fader->setSize(width, fader->getHeight());
                for (const double value :
                     {fader->getMinimum(), fader->getDoubleClickReturnValue(), fader->getMaximum()}) {
                    fader->setValue(value, juce::sendNotificationSync);
                    const auto m = synth::theme::fader::metricsFor(false, fader->getWidth(), fader->getHeight());
                    const auto cap = synth::theme::fader::capBounds(fader->travelBounds(),
                                                                    fader->getPositionOfValue(value), false, m);
                    const auto textBox = box->getBounds().toFloat();
                    const float textWidth = textWidthOf(*box);
                    const juce::String context = type + " " + fader->getComponentID() + " width " +
                                                 juce::String(width) + " value " + juce::String(value) + " text \"" +
                                                 box->getText() + "\" textWidth " + juce::String(textWidth);
                    EXPECT_FALSE(cap.expanded(kFocusRingReach + kRingToBoxGap).intersects(textBox))
                        << context << "\n  cap " << cap.toString() << "\n  box " << textBox.toString();
                    EXPECT_LE(textWidth, (float)box->getWidth()) << context << " does not fit its box";
                    ++checked;
                }
            }
            fader->setValue(original, juce::sendNotificationSync);
        }
    };
    for (const auto& type : types) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (dynamic_cast<ModuleBase*>(processor.get()) == nullptr)
            continue;
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        sweep(card, type);
    }

    // No default card draws a horizontal fader any more; a stored layout still can (a VCA's Gain here).
    auto vca = engine.getGraph().addNode(synth::AIStateMapper::createModule("VCA"));
    synth::CardParamItem gain;
    gain.paramId = "gain";
    gain.widget = synth::CardWidget::FaderH;
    synth::CardSection section;
    section.id = "main";
    section.items.emplace_back(gain);
    synth::CardLayout stored;
    stored.sections = {section};
    synth::setCardLayoutOverride(engine.getGraph(), nullptr, vca->nodeID, stored);
    ModuleComponent storedCard(vca->getProcessor(), vca->nodeID, editor);
    sweep(storedCard, "VCA (stored layout)");
    EXPECT_GT(checked, 0);
}

// Clicking a card's mode switch (the Poly pill) must not move the modules around it: neighbours are placed clear of the
// card, and after the click (and a second click back) each keeps its exact bounds. A card whose size changes with the
// switch may only push modules it now covers, and none of these are covered.
// Regression test for FRO2323: clicking Poly in the Filter was reported to move the modules around it to the left.

using namespace cardbody_test;

namespace {

constexpr int kClear = 3 * synth::LayoutUtil::kCollisionGap; // well clear of the card, so nothing is ever covered

// What the card looks like after its poly pill is clicked, and what each neighbour did.
void clickPolyAndExpectNeighboursStill(const juce::String& type) {
    SCOPED_TRACE(type);
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule(type), 800, 600);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    auto* body = card->getCardBody();
    ASSERT_NE(body, nullptr);
    auto* pill = dynamic_cast<juce::Button*>(body->findWidget("poly"));
    ASSERT_NE(pill, nullptr) << "a card with a poly parameter shows a poly pill";

    const auto home = card->getBounds();
    const auto probe = canvas.add(synth::AIStateMapper::createModule("Noise"), 0, 0);
    canvas.editor.updateComponents();
    const auto size = canvas.card(probe)->getBounds();
    canvas.engine.getGraph().removeNode(probe);
    canvas.editor.updateComponents();
    card = canvas.card(id);
    ASSERT_NE(card, nullptr);

    std::map<NodeID, juce::Rectangle<int>> expected;
    const std::vector<juce::Point<int>> spots = {
        {home.getRight() + kClear, home.getY()},
        {home.getX(), home.getBottom() + kClear},
        {home.getRight() + kClear, home.getBottom() + kClear},
        {home.getX() - size.getWidth() - kClear, home.getY()},
        {home.getX(), home.getY() - size.getHeight() - kClear},
    };
    for (const auto& spot : spots)
        expected[canvas.add(synth::AIStateMapper::createModule("Noise"), spot.x, spot.y)] = {};
    canvas.editor.updateComponents();
    card = canvas.card(id);
    ASSERT_NE(card, nullptr);
    ASSERT_EQ(card->getBounds().getPosition(), home.getPosition()) << "adding neighbours does not move the card";
    for (auto& [neighbour, rect] : expected) {
        ASSERT_NE(canvas.card(neighbour), nullptr);
        rect = canvas.card(neighbour)->getBounds();
        ASSERT_FALSE(rect.expanded(2).intersects(card->getBounds())) << "the neighbours start clear of the card";
    }

    for (int click = 0; click < 2; ++click) {
        auto* live = dynamic_cast<juce::Button*>(canvas.card(id)->getCardBody()->findWidget("poly"));
        ASSERT_NE(live, nullptr);
        live->triggerClick();
        synth::test::flushMessageQueue();
        ASSERT_NE(canvas.card(id), nullptr);
        EXPECT_EQ(canvas.card(id)->getPosition(), home.getPosition()) << "click " << click << ": the card moved";
        for (const auto& [neighbour, rect] : expected)
            if (auto* comp = canvas.card(neighbour))
                EXPECT_EQ(comp->getBounds(), rect)
                    << "click " << click << ": a neighbour moved (card is now "
                    << canvas.card(id)->getBounds().toString() << ", was " << home.toString() << ")";
    }
}

} // namespace

TEST(CardModeToggleNeighbours, ClickingPolyOnAnyCardLeavesTheModulesAroundItWhereTheyWere) {
    int checked = 0;
    for (const auto& type : libraryTypes()) {
        auto probe = synth::AIStateMapper::createModule(type);
        if (probe == nullptr || findParameterByID(probe.get(), "poly") == nullptr)
            continue;
        CardCanvas canvas;
        const auto id = canvas.add(std::move(probe), 0, 0);
        canvas.editor.updateComponents();
        if (canvas.card(id) == nullptr || canvas.card(id)->getCardBody() == nullptr ||
            canvas.card(id)->getCardBody()->findWidget("poly") == nullptr)
            continue; // no poly pill on this card
        clickPolyAndExpectNeighboursStill(type);
        ++checked;
    }
    EXPECT_GT(checked, 0);
}

TEST(CardModeToggleNeighbours, TheFilterIsAmongTheCardsWithAPolyPill) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Filter"), 0, 0);
    canvas.editor.updateComponents();
    ASSERT_NE(canvas.card(id), nullptr);
    EXPECT_NE(canvas.card(id)->getCardBody()->findWidget("poly"), nullptr);
}

// A real chain: the cards the click flips are wired together and stand a normal cable's length apart. Flipping Poly
// changes what each shows, never its size, so no card of the chain, and none beside it, may move.
TEST(CardModeToggleNeighbours, ClickingPolyInAWiredChainKeepsEveryCardWhereItWasAndItsSize) {
    CardCanvas canvas;
    const std::vector<juce::String> types = {"Oscillator", "Filter", "ADSR", "Delay", "Reverb"};
    std::vector<NodeID> ids;
    int x = 100;
    for (const auto& type : types) {
        ids.push_back(canvas.add(synth::AIStateMapper::createModule(type), x, 100));
        canvas.editor.updateComponents();
        x = canvas.card(ids.back())->getRight() + 3 * synth::LayoutUtil::kCollisionGap;
    }
    for (size_t i = 0; i + 1 < ids.size(); ++i)
        canvas.engine.getGraph().addConnection({{ids[i], 0}, {ids[i + 1], 0}});
    canvas.editor.updateComponents();

    std::vector<juce::Rectangle<int>> before;
    for (const auto id : ids) {
        ASSERT_NE(canvas.card(id), nullptr);
        before.push_back(canvas.card(id)->getBounds());
    }
    auto* pill = dynamic_cast<juce::Button*>(canvas.card(ids[1])->getCardBody()->findWidget("poly"));
    ASSERT_NE(pill, nullptr);
    for (int click = 0; click < 2; ++click) {
        dynamic_cast<juce::Button*>(canvas.card(ids[1])->getCardBody()->findWidget("poly"))->triggerClick();
        synth::test::flushMessageQueue();
        for (size_t i = 0; i < ids.size(); ++i)
            EXPECT_EQ(canvas.card(ids[i])->getBounds(), before[i]) << types[i] << " after click " << click;
    }
}

// The voice chain a track really has: a Sequencer on MIDI into the Oscillator, then audio on through the Filter. Poly
// inserts a Poly MIDI node between the Sequencer and the Oscillator; the cards that were there stay where they were
// (the new card takes a free place beside them).
TEST(CardModeToggleNeighbours, ClickingPolyOnAFilterOfASequencedChainLeavesTheExistingCardsWhereTheyWere) {
    CardCanvas canvas;
    const std::vector<juce::String> types = {"Sequencer", "Oscillator", "Filter", "ADSR", "Delay"};
    std::vector<NodeID> ids;
    int x = 100;
    for (const auto& type : types) {
        ids.push_back(canvas.add(synth::AIStateMapper::createModule(type), x, 100));
        canvas.editor.updateComponents();
        x = canvas.card(ids.back())->getRight() + 3 * synth::LayoutUtil::kCollisionGap;
    }
    auto& graph = canvas.engine.getGraph();
    const auto midi = juce::AudioProcessorGraph::midiChannelIndex;
    EXPECT_TRUE(graph.addConnection({{ids[0], midi}, {ids[1], midi}}));
    for (size_t i = 1; i + 1 < ids.size(); ++i)
        graph.addConnection({{ids[i], 0}, {ids[i + 1], 0}});
    canvas.editor.updateComponents();

    std::vector<juce::Rectangle<int>> before;
    for (const auto id : ids)
        before.push_back(canvas.card(id)->getBounds());
    auto* pill = dynamic_cast<juce::Button*>(canvas.card(ids[2])->getCardBody()->findWidget("poly"));
    ASSERT_NE(pill, nullptr);
    for (int click = 0; click < 2; ++click) {
        dynamic_cast<juce::Button*>(canvas.card(ids[2])->getCardBody()->findWidget("poly"))->triggerClick();
        synth::test::flushMessageQueue();
        for (size_t i = 0; i < ids.size(); ++i)
            EXPECT_EQ(canvas.card(ids[i])->getBounds(), before[i]) << types[i] << " after click " << click;
    }
}

// A saved project whose cards already crowd each other (a project saved by an earlier build: the Gate sits 10 px above
// the Compressor and its left edge is 4 px inside the Limiter, the Filter's left edge 4 px inside the Compressor). The
// click grows or changes nothing that reaches a card it did not already touch, so the make-room pass must leave every
// card where it was: resolving the old crowding would send the Gate 400 px left and the others sideways.
TEST(CardModeToggleNeighbours, ClickingPolyInAProjectWithOldOverlapsMovesNoCard) {
    CardCanvas canvas;
    struct Spot {
        const char* type;
        int x, y;
    };
    const std::vector<Spot> spots = {
        {"Gate", 704, 392}, {"Limiter", 408, 392}, {"Compressor", 704, 704}, {"Filter", 1000, 872}};
    std::vector<NodeID> ids;
    for (const auto& spot : spots)
        ids.push_back(canvas.add(synth::AIStateMapper::createModule(spot.type), spot.x, spot.y));
    canvas.editor.updateComponents();
    std::vector<juce::Rectangle<int>> before;
    for (const auto id : ids) {
        ASSERT_NE(canvas.card(id), nullptr);
        before.push_back(canvas.card(id)->getBounds());
    }
    for (int click = 0; click < 2; ++click) {
        auto* pill = dynamic_cast<juce::Button*>(canvas.card(ids.back())->getCardBody()->findWidget("poly"));
        ASSERT_NE(pill, nullptr);
        pill->triggerClick();
        synth::test::flushMessageQueue();
        for (size_t i = 0; i < ids.size(); ++i)
            EXPECT_EQ(canvas.card(ids[i])->getPosition(), before[i].getPosition())
                << spots[i].type << " after click " << click;
    }
}

// Every built-in module type that has a code default draws from it when nothing is saved: no card falls back to the
// automatic list or a hand-painted layout. Types are discovered (the library's, the extras and the registry's), never
// listed here.
TEST(CardLayoutResolution, EveryBuiltInTypeWithACodeDefaultResolvesFromItWhenNothingIsSaved) {
    std::set<juce::String> types;
    ModuleLibraryComponent library;
    for (const auto& type : library.getDraggableModuleNames())
        types.insert(type);
    for (const auto& type : synth::DefaultCardLayouts::builtIn().types())
        types.insert(type);
    int withDefault = 0;
    AudioEngine engine;
    GraphEditor editor(engine);
    for (const auto& type : types) {
        const auto* entry = synth::DefaultCardLayouts::builtIn().find(type);
        if (entry == nullptr)
            continue;
        ++withDefault;
        // The card is a CardBody drawn from a layout, not a hand-painted body.
        auto processor = synth::AIStateMapper::createModule(type);
        ASSERT_NE(processor, nullptr) << type;
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
        ASSERT_NE(card.getCardBody(), nullptr) << type << " has no CardBody";
        EXPECT_TRUE(card.getCardBody()->drawsFromLayout()) << type << " does not draw from a layout";
        const auto resolved =
            synth::resolveModuleCardLayout(type, juce::var(), nullptr, synth::DefaultCardLayouts::builtIn());
        EXPECT_EQ(resolved.source, synth::ResolvedModuleCardLayout::Source::CodeDefault) << type;
        EXPECT_EQ(resolved.defaultRevision, entry->defaultRevision) << type;
        // A layout saved at any earlier revision (or none) cannot hide it.
        for (int old = 0; old < entry->defaultRevision; ++old) {
            auto stale = entry->layout;
            stale.basedOn = type + "@" + juce::String(old);
            const auto fallthrough =
                synth::resolveModuleCardLayout(type, stale.toVar(), nullptr, synth::DefaultCardLayouts::builtIn());
            EXPECT_EQ(fallthrough.source, synth::ResolvedModuleCardLayout::Source::CodeDefault)
                << type << " saved at revision " << old;
        }
        auto unmarked = entry->layout;
        unmarked.basedOn.reset();
        EXPECT_EQ(synth::resolveModuleCardLayout(type, unmarked.toVar(), nullptr, synth::DefaultCardLayouts::builtIn())
                      .source,
                  synth::ResolvedModuleCardLayout::Source::CodeDefault)
            << type << " saved with no mark";
    }
    EXPECT_GT(withDefault, 20);
}
