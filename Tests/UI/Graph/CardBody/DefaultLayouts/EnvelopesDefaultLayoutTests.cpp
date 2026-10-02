// EnvelopesDefaultLayoutTests.cpp
//
// The designed default card layouts of the "Envelopes and utilities" family (Source/UI/Graph/CardBody/
// DefaultLayouts/DefaultCardLayoutsEnvelopes.cpp): ADSR (and its Amp Env and Filter Env keys), VCA, Envelope
// Follower, Sample & Hold, Math, Voice Mixer, Poly MIDI, and the MIDI Keyboard's Octave stepper row. Each
// card has the designed sections in order and the named widgets; the conditions behave (a swap keeps its
// bounds and the card's height, a conditional section changes the height only on the mode switch); every
// control is a keyboard stop with an accessible title and a tooltip; and the size estimate equals the real
// card. docs/layout/module-card-layout.md#default-layouts.

#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ADSRModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardBody/DefaultCardLayouts.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Graph/CardWidgets/CardStepper.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using Kind = synth::CardBodyItem::Kind;

namespace {

// A section of the plan as text: each parameter's id, a view as "view:<name>", in card order.
juce::StringArray sectionItems(const synth::CardBodyPlan& plan, int section) {
    juce::StringArray items;
    for (int index : plan.sections[(size_t)section].items) {
        const auto& item = plan.items[(size_t)index];
        if (item.kind == Kind::View)
            items.add(item.view == synth::CardView::Envelope    ? "view:envelope"
                      : item.view == synth::CardView::Threshold ? "view:threshold"
                                                                : "view:other");
        else
            items.add(item.param->paramID);
    }
    return items;
}

Kind kindOf(const synth::CardBodyPlan& plan, const juce::String& paramId) {
    const int index = plan.findParam(paramId);
    return index >= 0 ? plan.items[(size_t)index].kind : Kind::View;
}

bool isPill(const juce::Component& component) {
    return (bool)component.getProperties()[synth::theme::AppLookAndFeel::kTogglePillProperty];
}

juce::ToggleButton* chromeToggle(ModuleComponent& card, const juce::String& text) {
    for (auto* child : card.getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
            toggle != nullptr && toggle->getButtonText() == text)
            return toggle;
    return nullptr;
}

// A control the keyboard reaches: itself, or (a switch, a stepper) one of its parts.
bool keyboardReachable(juce::Component& control) {
    if (control.getWantsKeyboardFocus())
        return true;
    for (auto* child : control.getChildren())
        if (child->getWantsKeyboardFocus())
            return true;
    return false;
}

void setParam(juce::AudioProcessor& module, const juce::String& id, float plainValue) {
    auto* param = findParameterByID(&module, id);
    ASSERT_NE(param, nullptr) << id;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

// Sets a parameter as automation would, then runs the card's queued condition re-read.
void flip(CardCanvas& canvas, NodeID id, const juce::String& paramId, float plainValue) {
    setParam(*canvas.processor(id), paramId, plainValue);
    canvas.card(id)->getCardBody()->flushPendingConditionUpdate();
}

const juce::StringArray kAdsrTypes{"ADSR", "Amp Env", "Filter Env"};

const juce::StringArray kBodyTypes{"ADSR",          "Amp Env", "Filter Env",  "VCA",      "Envelope Follower",
                                   "Sample & Hold", "Math",    "Voice Mixer", "Poly MIDI"};

} // namespace

TEST(EnvelopesDefaultLayout, EveryTypeHasACodeDefaultAtRevisionOne) {
    for (const auto& type : kBodyTypes) {
        const auto* entry = synth::DefaultCardLayouts::builtIn().find(type);
        ASSERT_NE(entry, nullptr) << type;
        EXPECT_EQ(entry->defaultRevision, 1) << type;
    }
}

TEST(EnvelopesDefaultLayout, TheAdsrCardHasTheDesignedSectionsInOrder) {
    for (const auto& type : kAdsrTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        const auto& plan = canvas.card(id)->getCardBody()->getPlan();
        ASSERT_EQ(plan.sections.size(), 5u);
        EXPECT_EQ(sectionItems(plan, 0), juce::StringArray({"view:envelope"}));
        EXPECT_EQ(sectionItems(plan, 1), juce::StringArray({"tempoSync"}));
        EXPECT_EQ(sectionItems(plan, 2), juce::StringArray({"attack", "attackDiv", "hold", "holdDiv", "decay",
                                                            "decayDiv", "sustain", "release", "releaseDiv"}));
        EXPECT_EQ(sectionItems(plan, 3), juce::StringArray({"velocity", "view:threshold"}));
        EXPECT_TRUE(plan.sections[4].footer);
        EXPECT_EQ(sectionItems(plan, 4), juce::StringArray({"poly"})) << "Poly joins the footer by itself";
        EXPECT_EQ(plan.sections[2].columns, 5);

        EXPECT_EQ(kindOf(plan, "tempoSync"), Kind::Segmented);
        for (const char* stage : {"attack", "hold", "decay", "sustain", "release"})
            EXPECT_EQ(kindOf(plan, stage), Kind::FaderV) << stage;
        for (const char* division : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"})
            EXPECT_EQ(kindOf(plan, division), Kind::FaderV) << division << ": a stepped fader beside its time";
        EXPECT_EQ(kindOf(plan, "velocity"), Kind::FaderH);
        EXPECT_TRUE(plan.more.empty()) << "every parameter but the three curve amounts the graph edits is placed";
        EXPECT_EQ(plan.swapGroups.size(), 4u) << "one swap group per stage: time or its division";
    }
}

TEST(EnvelopesDefaultLayout, TheAdsrEnvelopeViewIsOpenAndTheThresholdViewOwnsItsParameter) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    auto* envelope = body->findView(synth::CardView::Envelope);
    ASSERT_NE(envelope, nullptr);
    EXPECT_TRUE(envelope->isVisible());
    EXPECT_TRUE(card->getLocalBounds().contains(envelope->getBounds()));
    ASSERT_NE(body->getThresholdView(), nullptr);
    EXPECT_EQ(body->findWidget("gateThreshold"), nullptr) << "the threshold view edits gateThreshold itself";
    EXPECT_LT(envelope->getBottom(), body->findWidget("tempoSync")->getY()) << "the graph is the top section";
}

// A fader is 40 px wide: its value box must hold the longest reading ("1.00s", "120ms", "100%") whole,
// not cut it to "1.00...".
TEST(EnvelopesDefaultLayout, TheStageFadersValueBoxesHoldEveryReadingWithoutTruncating) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* body = canvas.card(id)->getCardBody();
    for (const char* paramId : {"attack", "hold", "decay", "sustain", "release"}) {
        SCOPED_TRACE(paramId);
        auto* fader = dynamic_cast<synth::ui::CardFader*>(body->findWidget(paramId));
        ASSERT_NE(fader, nullptr);
        juce::Label* box = nullptr;
        for (auto* child : fader->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child))
                box = label;
        ASSERT_NE(box, nullptr);
        for (const double value : {fader->getMinimum(), fader->getMaximum(), fader->getValue(), 9.5, 99.5}) {
            const auto text = fader->getTextFromValue(juce::jlimit(fader->getMinimum(), fader->getMaximum(), value));
            EXPECT_FALSE(text.contains(" ")) << text;
            const auto available = box->getBorderSize().subtractedFrom(box->getLocalBounds()).getWidth();
            EXPECT_LE(juce::GlyphArrangement::getStringWidthInt(box->getFont(), text), available) << text;
        }
    }
}

// In Tempo mode each stage's division is a stepped vertical fader over the choice's steps: every division
// reads whole in the 40 px value box, a step moves it one division, and the arrow keys walk the list.
TEST(EnvelopesDefaultLayout, EveryDivisionFitsItsStageCellWithoutTruncating) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 0, 0);
    canvas.editor.updateComponents();
    flip(canvas, id, "tempoSync", 1.0f);
    auto* body = canvas.card(id)->getCardBody();
    for (const char* paramId : {"attackDiv", "holdDiv", "decayDiv", "releaseDiv"}) {
        SCOPED_TRACE(paramId);
        auto* fader = dynamic_cast<synth::ui::CardFader*>(body->findWidget(paramId));
        ASSERT_NE(fader, nullptr);
        ASSERT_TRUE(fader->isVisible());
        auto* choice = dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(canvas.processor(id), paramId));
        ASSERT_NE(choice, nullptr);
        EXPECT_EQ(fader->getInterval(), 1.0) << "stepped over the choice's steps";
        EXPECT_EQ(fader->getMinimum(), 0.0);
        EXPECT_EQ(fader->getMaximum(), (double)(choice->choices.size() - 1));
        juce::Label* box = nullptr;
        for (auto* child : fader->getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child))
                box = label;
        ASSERT_NE(box, nullptr);
        const auto available = box->getBorderSize().subtractedFrom(box->getLocalBounds()).getWidth();
        EXPECT_GE(available, 36);
        for (int step = 0; step < choice->choices.size(); ++step) {
            const auto text = fader->getTextFromValue((double)step);
            EXPECT_EQ(text, synth::ui::CardFader::compactValueText(choice->choices[step]));
            EXPECT_LE(juce::GlyphArrangement::getStringWidthInt(box->getFont(), text), available) << text;
        }
        fader->setValue(0.0, juce::sendNotificationSync);
        EXPECT_EQ(choice->getIndex(), 0);
        EXPECT_TRUE(fader->keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
        EXPECT_EQ(choice->getIndex(), 1) << "an arrow key steps one division";
        EXPECT_TRUE(fader->keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
        EXPECT_EQ(choice->getIndex(), 0);
    }
}

TEST(EnvelopesDefaultLayout, AStageSwapKeepsItsBoundsAndTheCardsHeight) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    const int height = card->getHeight();
    struct Stage {
        const char* time;
        const char* division;
    };
    for (const Stage stage : {Stage{"attack", "attackDiv"}, Stage{"hold", "holdDiv"}, Stage{"decay", "decayDiv"},
                              Stage{"release", "releaseDiv"}}) {
        SCOPED_TRACE(stage.time);
        auto* fader = body->findWidget(stage.time);
        auto* combo = body->findWidget(stage.division);
        ASSERT_NE(fader, nullptr);
        ASSERT_NE(combo, nullptr);
        const auto faderBounds = fader->getBounds();
        EXPECT_TRUE(fader->isVisible());
        EXPECT_FALSE(combo->isVisible());

        flip(canvas, id, "tempoSync", 1.0f);
        EXPECT_FALSE(fader->isVisible());
        EXPECT_TRUE(combo->isVisible());
        EXPECT_EQ(fader->getBounds(), faderBounds) << "a swapped-out fader keeps its cell";
        EXPECT_EQ(combo->getBounds(), faderBounds) << "the division fills the cell of the fader it replaces";
        EXPECT_EQ(card->getHeight(), height);

        flip(canvas, id, "tempoSync", 0.0f);
        EXPECT_TRUE(fader->isVisible());
        EXPECT_FALSE(combo->isVisible());
        EXPECT_EQ(card->getHeight(), height);
    }
    EXPECT_TRUE(body->findWidget("sustain")->isVisible()) << "Sustain has no division";
}

TEST(EnvelopesDefaultLayout, TheEnvelopeToggleClosesAndReopensTheViewAndTheFooterHoldsTheTogglePills) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<ADSRModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* toggle = chromeToggle(*card, "Show Envelope Graph");
    auto* scope = chromeToggle(*card, "Show Scope");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(scope, nullptr);
    EXPECT_TRUE(isPill(*toggle));
    EXPECT_TRUE(isPill(*scope));
    EXPECT_TRUE(isPill(*card->getCardBody()->findWidget("poly")));
    EXPECT_GE(toggle->getY(), card->getCardBody()->findWidget("velocity")->getBottom());

    const int height = card->getHeight();
    toggle->setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(card->getCardBody()->findView(synth::CardView::Envelope)->isVisible());
    EXPECT_LT(card->getHeight(), height);
    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_EQ(card->getHeight(), height);
}

TEST(EnvelopesDefaultLayout, TheSimpleCardsHaveTheirDesignedSections) {
    struct Expected {
        const char* type;
        std::vector<juce::StringArray> sections;
        bool footer;
    };
    const std::vector<Expected> expected{
        {"VCA", {{"gain"}}, true},
        {"Envelope Follower", {{"detection", "attack", "release", "sensitivity"}}, true},
        {"Math", {{"clip"}}, true},
        {"Voice Mixer", {{"level"}}, false},
        {"Poly MIDI", {{"voiceSteal"}}, true},
        {"Sample & Hold",
         {{"source", "holdMode", "clock"}, {"view:threshold"}, {"rate", "trigThreshold", "slew", "level", "offset"}},
         false}};
    for (const auto& want : expected) {
        SCOPED_TRACE(want.type);
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(want.type), 0, 0);
        canvas.editor.updateComponents();
        const auto& plan = canvas.card(id)->getCardBody()->getPlan();
        ASSERT_EQ(plan.sections.size(), want.sections.size() + (want.footer ? 1u : 0u));
        for (size_t i = 0; i < want.sections.size(); ++i)
            EXPECT_EQ(sectionItems(plan, (int)i), want.sections[i]) << "section " << i;
        EXPECT_EQ(plan.hasFooter(), want.footer);
    }
}

TEST(EnvelopesDefaultLayout, TheNamedWidgetsAreTheDesignedKinds) {
    CardCanvas canvas;
    const auto vca = canvas.add(synth::AIStateMapper::createModule("VCA"), 0, 0);
    const auto follower = canvas.add(synth::AIStateMapper::createModule("Envelope Follower"), 400, 0);
    const auto math = canvas.add(synth::AIStateMapper::createModule("Math"), 800, 0);
    const auto mixer = canvas.add(synth::AIStateMapper::createModule("Voice Mixer"), 1200, 0);
    const auto poly = canvas.add(synth::AIStateMapper::createModule("Poly MIDI"), 1600, 0);
    canvas.editor.updateComponents();
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(canvas.card(vca)->getCardBody()->findWidget("gain")), nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::CardFader*>(canvas.card(mixer)->getCardBody()->findWidget("level")), nullptr);
    EXPECT_NE(
        dynamic_cast<synth::ui::CardSegmentedSwitch*>(canvas.card(follower)->getCardBody()->findWidget("detection")),
        nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::CardSegmentedSwitch*>(canvas.card(math)->getCardBody()->findWidget("clip")),
              nullptr);
    // "Round-Robin" is longer than a switch's 10 characters, so Voice Steal is the combo.
    EXPECT_NE(dynamic_cast<juce::ComboBox*>(canvas.card(poly)->getCardBody()->findWidget("voiceSteal")), nullptr);
    auto* velToGate = canvas.card(poly)->getCardBody()->findWidget("velToGate");
    ASSERT_NE(velToGate, nullptr);
    EXPECT_TRUE(isPill(*velToGate));
    EXPECT_EQ(dynamic_cast<juce::Button*>(velToGate)->getButtonText(), "Velocity sets gate");
    EXPECT_TRUE(isPill(*canvas.card(vca)->getCardBody()->findWidget("poly"))) << "Poly joins the footer";
    EXPECT_EQ(canvas.card(mixer)->getCardBody()->findWidget("poly"), nullptr) << "Voice Mixer has no Poly";
}

// The clock decides the first knob cell: the internal clock's Rate, or the external clock's trigger
// Threshold with its meter above. The cell is a swap (bounds and jacks stay); only the Clock switch, a
// deliberate mode choice, changes the card's height (the meter opening).
TEST(EnvelopesDefaultLayout, SampleAndHoldSwapsRateForTheThresholdAndOpensItsMeterWithTheClock) {
    CardCanvas canvas;
    const auto id = canvas.add(synth::AIStateMapper::createModule("Sample & Hold"), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* body = card->getCardBody();
    auto* rate = body->findWidget("rate");
    auto* threshold = body->findWidget("trigThreshold");
    auto* meter = body->findView(synth::CardView::Threshold);
    ASSERT_NE(rate, nullptr);
    ASSERT_NE(threshold, nullptr);
    ASSERT_NE(meter, nullptr);
    EXPECT_TRUE(rate->isVisible());
    EXPECT_FALSE(threshold->isVisible());
    EXPECT_FALSE(meter->isVisible());
    const int internalHeight = card->getHeight();
    const auto rateBounds = rate->getBounds();
    const auto jacks = card->drawnInputJackIndices();

    flip(canvas, id, "clock", 1.0f); // External
    EXPECT_FALSE(rate->isVisible());
    EXPECT_TRUE(threshold->isVisible());
    EXPECT_TRUE(meter->isVisible());
    EXPECT_GT(card->getHeight(), internalHeight) << "the trigger meter opens";
    EXPECT_TRUE(card->getLocalBounds().contains(meter->getBounds()));
    EXPECT_EQ(threshold->getX(), rateBounds.getX()) << "the Threshold takes Rate's cell";
    EXPECT_EQ(threshold->getY() - rate->getY(), 0);
    EXPECT_EQ(card->drawnInputJackIndices(), jacks) << "a swapped-out knob keeps its jack, so the gutter is steady";

    // A value change on any control never resizes the card.
    const int externalHeight = card->getHeight();
    flip(canvas, id, "trigThreshold", -0.5f);
    flip(canvas, id, "slew", 0.7f);
    EXPECT_EQ(card->getHeight(), externalHeight);

    flip(canvas, id, "clock", 0.0f);
    EXPECT_TRUE(rate->isVisible());
    EXPECT_FALSE(threshold->isVisible());
    EXPECT_EQ(card->getHeight(), internalHeight) << "the height comes back with Internal";
}

// Every control of every card is a keyboard stop with an accessible title and a tooltip, section titles
// (none here) never shout, and nothing is drawn in capitals.
TEST(EnvelopesDefaultLayout, EveryControlIsKeyboardReachableWithATitleAndATooltip) {
    for (const auto& type : kBodyTypes) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        for (auto& item : card->getCardBody()->getPlan().items) {
            if (item.kind == Kind::View || item.widget == nullptr)
                continue;
            SCOPED_TRACE(item.param->paramID.toStdString());
            EXPECT_TRUE(keyboardReachable(*item.widget));
            EXPECT_TRUE(item.widget->getTitle().isNotEmpty());
            auto* tip = dynamic_cast<juce::SettableTooltipClient*>(item.widget);
            ASSERT_NE(tip, nullptr);
            EXPECT_TRUE(tip->getTooltip().isNotEmpty());
        }
        for (const auto& section : card->getCardBody()->getPlan().sections)
            if (section.title.has_value())
                EXPECT_NE(*section.title, section.title->toUpperCase()) << "section titles are sentence case";
    }
}

// The estimate (the drag ghost and the drop placement) equals the real card, for every card of the family
// including the MIDI Keyboard's bespoke table entry.
TEST(EnvelopesDefaultLayout, TheEstimateEqualsTheRealCard) {
    juce::StringArray types = kBodyTypes;
    types.add("MIDI Keyboard");
    for (const auto& type : types) {
        SCOPED_TRACE(type.toStdString());
        CardCanvas canvas;
        const auto id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
        canvas.editor.updateComponents();
        auto* card = canvas.card(id);
        EXPECT_EQ(GraphEditor::estimateModuleSize(type), juce::Point<int>(card->getWidth(), card->getHeight()));
    }
}

TEST(EnvelopesDefaultLayout, TheMidiKeyboardCardHasAnOctaveStepperRowAboveTheKeys) {
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<MidiKeyboardModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    synth::ui::CardStepper* stepper = nullptr;
    juce::Component* keys = nullptr;
    for (auto* child : card->getChildren()) {
        if (auto* found = dynamic_cast<synth::ui::CardStepper*>(child))
            stepper = found;
        if (child->getTitle() == "Keyboard")
            keys = child;
    }
    ASSERT_NE(stepper, nullptr);
    ASSERT_NE(keys, nullptr);
    EXPECT_TRUE(stepper->isVisible());
    EXPECT_LE(stepper->getBottom(), keys->getY()) << "the row sits above the keys";
    EXPECT_TRUE(card->getLocalBounds().contains(stepper->getBounds()));
    EXPECT_TRUE(card->getLocalBounds().contains(keys->getBounds()));

    // Reachable and named: two Tab stops titled after the parameter, and a tooltip.
    EXPECT_EQ(stepper->getTitle(), "Octave");
    EXPECT_EQ(stepper->getTooltip(), "Octave");
    EXPECT_TRUE(stepper->getDownButton().getWantsKeyboardFocus());
    EXPECT_TRUE(stepper->getUpButton().getWantsKeyboardFocus());
    EXPECT_EQ(stepper->getDownButton().getTitle(), "Octave down");
    EXPECT_EQ(stepper->getUpButton().getTitle(), "Octave up");

    // It drives the module's octave parameter, one change gesture per step, and follows external writes.
    auto* octave = dynamic_cast<juce::AudioParameterInt*>(findParameterByID(canvas.processor(id), "octave"));
    ASSERT_NE(octave, nullptr);
    EXPECT_EQ(stepper->getValueText(), octave->getCurrentValueAsText());
    stepper->getUpButton().onClick();
    EXPECT_EQ(octave->get(), 1);
    stepper->getUpButton().onClick();
    stepper->getUpButton().onClick(); // clamped at the top of the range
    EXPECT_EQ(octave->get(), 2);
    stepper->getDownButton().onClick();
    EXPECT_EQ(octave->get(), 1);
    EXPECT_EQ(stepper->getValueText(), octave->getCurrentValueAsText());
    setParam(*canvas.processor(id), "octave", -2.0f);
    EXPECT_EQ(stepper->getValueText(), octave->getCurrentValueAsText());
}
