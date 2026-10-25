// SourcesDefaultLayoutTests.cpp
//
// The designed default card layouts of the Sources family (Oscillator, Noise, Sampler, LFO), drawn by
// real cards on a canvas with no override: the sections in order with their titles, the widgets the
// design names, the conditions (a swap keeps its cell, a dim keeps its cell and stays focusable, a
// section appears on a mode switch), the footer, and the size estimate against the real card.
// Source/UI/Graph/CardBody/DefaultLayouts/DefaultCardLayoutsSources.cpp.

#include "../../../Layout/NativeWindowTestGuard.h"
#include "../../GraphEditor/GraphEditorTestHelpers.h"
#include "../CardBodyTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "UI/Graph/CardBody/CardBodyMeasure.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/ModuleViews/SampleWaveformComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

using namespace cardbody_test;
using synth::theme::AppLookAndFeel;

namespace {

struct Card {
    NodeID id;
    ModuleComponent* card = nullptr;
    synth::CardBody* body = nullptr;
};

Card build(CardCanvas& canvas, const juce::String& type) {
    Card result;
    result.id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
    canvas.editor.updateComponents();
    result.card = canvas.card(result.id);
    result.body = result.card->getCardBody();
    return result;
}

void flip(CardCanvas& canvas, const Card& card, const juce::String& paramId, float plainValue) {
    auto* param = findParameterByID(canvas.processor(card.id), paramId);
    ASSERT_NE(param, nullptr) << paramId;
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
    card.body->flushPendingConditionUpdate();
}

// The titles of the body's sections, top to bottom, the footer left out; "" for an untitled one.
std::vector<juce::String> sectionTitles(const Card& card) {
    std::vector<juce::String> titles;
    for (const auto& section : card.body->getPlan().sections)
        if (!section.footer)
            titles.push_back(section.title.value_or(juce::String()));
    return titles;
}

synth::CardBodyItem::Kind kindOf(const Card& card, const juce::String& paramId) {
    const auto& plan = card.body->getPlan();
    return plan.items[(size_t)plan.findParam(paramId)].kind;
}

bool isPill(const juce::Component* component) {
    return component != nullptr && (bool)component->getProperties()[AppLookAndFeel::kTogglePillProperty];
}

juce::Component* widget(const Card& card, const juce::String& paramId) { return card.body->findWidget(paramId); }

// The widgets named by `paramIds` sit top to bottom in that order (a repeated row is allowed).
void expectTopToBottom(const Card& card, std::initializer_list<const char*> paramIds) {
    int previousY = -1;
    for (const auto* paramId : paramIds) {
        auto* w = widget(card, paramId);
        ASSERT_NE(w, nullptr) << paramId;
        EXPECT_GE(w->getY(), previousY) << paramId << " is out of order";
        previousY = w->getY();
    }
}

void expectOnOneRow(const Card& card, std::initializer_list<const char*> paramIds) {
    const int y = widget(card, *paramIds.begin())->getY();
    for (const auto* paramId : paramIds)
        EXPECT_EQ(widget(card, paramId)->getY(), y) << paramId;
}

// Greyed out but still a control: enabled, a Tab stop, with its caption greyed too.
void expectDimmedAndOperable(const Card& card, const juce::String& paramId, bool dimmed) {
    SCOPED_TRACE(paramId.toStdString());
    auto* w = widget(card, paramId);
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(AppLookAndFeel::paintsDimmed(*w), dimmed);
    EXPECT_TRUE(w->isEnabled());
    EXPECT_TRUE(w->getWantsKeyboardFocus());
    const auto tabbable = card.card->createKeyboardFocusTraverser()->getAllComponents(card.card);
    EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), w), tabbable.end());
}

} // namespace

// ---- Oscillator -------------------------------------------------------------------------------------

TEST(SourcesDefaultLayout, OscillatorHasItsDesignedSectionsAndWidgets) {
    CardCanvas canvas;
    const auto osc = build(canvas, "Oscillator");
    EXPECT_EQ(sectionTitles(osc), (std::vector<juce::String>{"", "Pitch", "Unison", "Output"}));
    EXPECT_EQ(kindOf(osc, "waveform"), synth::CardBodyItem::Kind::Segmented);
    EXPECT_NE(dynamic_cast<synth::ui::CardSegmentedSwitch*>(widget(osc, "waveform")), nullptr);
    expectTopToBottom(osc, {"waveform", "octave", "unison", "level"});
    expectOnOneRow(osc, {"octave", "coarse", "fine", "glide"});
    expectOnOneRow(osc, {"unison", "detune", "blend", "width"});
    expectOnOneRow(osc, {"level", "pan"});
    EXPECT_TRUE(osc.body->hasMoreRow()) << "Phase and Rand wait in the More controls row";
    ASSERT_TRUE(osc.body->hasFooter());
    EXPECT_TRUE(isPill(widget(osc, "poly"))) << "Poly joins the footer on its own";
}

TEST(SourcesDefaultLayout, OscillatorBlendKnobReadsAsPercentAtItsNewDefault) {
    CardCanvas canvas;
    const auto osc = build(canvas, "Oscillator");
    auto* blend = dynamic_cast<juce::Slider*>(widget(osc, "blend"));
    ASSERT_NE(blend, nullptr);
    EXPECT_EQ(blend->getTextFromValue(blend->getValue()), "25 %");
    EXPECT_NEAR(blend->getValueFromText("60"), 0.6, 1e-6);
    EXPECT_NEAR(blend->getValueFromText("60%"), 0.6, 1e-6);
}

TEST(SourcesDefaultLayout, OscillatorDetuneDimsAtOneVoiceAndPulseWidthUnlessSquare) {
    CardCanvas canvas;
    const auto osc = build(canvas, "Oscillator");
    const auto detune = widget(osc, "detune")->getBounds();
    const auto pulse = widget(osc, "pulseWidth")->getBounds();
    const auto size = osc.card->getBounds();
    expectDimmedAndOperable(osc, "detune", true);
    expectDimmedAndOperable(osc, "blend", true);
    expectDimmedAndOperable(osc, "width", true);
    expectDimmedAndOperable(osc, "pulseWidth", true);

    flip(canvas, osc, "unison", 3.0f);
    expectDimmedAndOperable(osc, "detune", false);
    expectDimmedAndOperable(osc, "blend", false);
    expectDimmedAndOperable(osc, "width", false);
    expectDimmedAndOperable(osc, "pulseWidth", true);
    flip(canvas, osc, "waveform", 1.0f); // Square
    expectDimmedAndOperable(osc, "pulseWidth", false);
    flip(canvas, osc, "waveform", 2.0f); // Saw
    expectDimmedAndOperable(osc, "pulseWidth", true);
    flip(canvas, osc, "unison", 1.0f);
    expectDimmedAndOperable(osc, "detune", true);

    EXPECT_EQ(widget(osc, "detune")->getBounds(), detune) << "a dim keeps its cell";
    EXPECT_EQ(widget(osc, "pulseWidth")->getBounds(), pulse);
    EXPECT_EQ(osc.card->getBounds(), size) << "and never resizes the card";
}

// ---- Noise ------------------------------------------------------------------------------------------

TEST(SourcesDefaultLayout, NoiseHasATypeSwitchThenColorAndLevelThenTheFooter) {
    CardCanvas canvas;
    const auto noise = build(canvas, "Noise");
    EXPECT_EQ(kindOf(noise, "noiseType"), synth::CardBodyItem::Kind::Segmented);
    expectTopToBottom(noise, {"noiseType", "color", "level"});
    expectOnOneRow(noise, {"color", "level"});
    EXPECT_FALSE(noise.body->hasMoreRow());
    ASSERT_TRUE(noise.body->hasFooter());
    EXPECT_TRUE(isPill(widget(noise, "poly")));
}

// ---- Sampler ----------------------------------------------------------------------------------------

TEST(SourcesDefaultLayout, SamplerHasItsWaveformThenModeRegionPitchAndAFooterWithLoopAndReverse) {
    CardCanvas canvas;
    const auto sampler = build(canvas, "Sampler");
    EXPECT_EQ(sectionTitles(sampler), (std::vector<juce::String>{"", "", "Pitch", "Grains", "Sound"}));
    EXPECT_EQ(kindOf(sampler, "playMode"), synth::CardBodyItem::Kind::Segmented);
    expectTopToBottom(sampler, {"playMode", "start", "pitch"});
    expectOnOneRow(sampler, {"start", "end", "level"});
    expectOnOneRow(sampler, {"pitch", "rootNote", "fine"});
    EXPECT_FALSE(sampler.body->hasMoreRow());

    // The waveform and its load row are card chrome above the body: one panel, not two.
    int waveforms = 0;
    for (auto* child : sampler.card->getChildren())
        if (dynamic_cast<SampleWaveformComponent*>(child) != nullptr) {
            ++waveforms;
            EXPECT_LT(child->getBottom(), widget(sampler, "playMode")->getY());
        }
    EXPECT_EQ(waveforms, 1);

    ASSERT_TRUE(sampler.body->hasFooter());
    for (const auto* paramId : {"loop", "reverse"})
        EXPECT_TRUE(isPill(widget(sampler, paramId))) << paramId;
    EXPECT_GT(widget(sampler, "loop")->getY(), widget(sampler, "fine")->getBottom());
}

TEST(SourcesDefaultLayout, SamplerGrainsDimUntilTheModeIsGranular) {
    CardCanvas canvas;
    const auto sampler = build(canvas, "Sampler");
    const auto size = sampler.card->getBounds();
    const auto grainSize = widget(sampler, "grainSize")->getBounds();
    expectOnOneRow(sampler, {"grainSize", "density", "spray"});
    EXPECT_GT(widget(sampler, "grainSize")->getY(), widget(sampler, "pitch")->getBottom());
    EXPECT_LT(widget(sampler, "grainSize")->getBottom(), widget(sampler, "loop")->getY()) << "above the footer";
    for (const auto* paramId : {"grainSize", "density", "spray"}) {
        EXPECT_TRUE(widget(sampler, paramId)->isVisible()) << paramId << " keeps its cell and its CV jack";
        expectDimmedAndOperable(sampler, paramId, true);
    }

    flip(canvas, sampler, "playMode", 1.0f);
    for (const auto* paramId : {"grainSize", "density", "spray"})
        expectDimmedAndOperable(sampler, paramId, false);
    flip(canvas, sampler, "playMode", 0.0f);
    expectDimmedAndOperable(sampler, "density", true);
    EXPECT_EQ(widget(sampler, "grainSize")->getBounds(), grainSize);
    EXPECT_EQ(sampler.card->getBounds(), size) << "a mode switch never resizes the card";
    EXPECT_FALSE(sampler.body->hasMoreRow());
}

// ---- LFO --------------------------------------------------------------------------------------------

TEST(SourcesDefaultLayout, LfoHasItsDesignedWidgetsInOrder) {
    CardCanvas canvas;
    const auto lfo = build(canvas, "LFO");
    EXPECT_EQ(kindOf(lfo, "shape"), synth::CardBodyItem::Kind::Choice) << "six values do not fit one switch";
    EXPECT_EQ(kindOf(lfo, "mode"), synth::CardBodyItem::Kind::Segmented);
    EXPECT_EQ(kindOf(lfo, "rateHz"), synth::CardBodyItem::Kind::KnobLarge);
    expectTopToBottom(lfo, {"shape", "mode", "rateHz", "phase"});
    expectOnOneRow(lfo, {"phase", "fadeIn", "level", "glide"});
    EXPECT_EQ(widget(lfo, "rateHz")->getHeight(), 80) << "the large dial";
    EXPECT_FALSE(lfo.body->hasMoreRow());
    ASSERT_TRUE(lfo.body->hasFooter());
    for (const auto* paramId : {"bipolar", "retrig"})
        EXPECT_TRUE(isPill(widget(lfo, paramId))) << paramId;
}

TEST(SourcesDefaultLayout, LfoRateSwapsHzAndDivisionInOneCell) {
    CardCanvas canvas;
    const auto lfo = build(canvas, "LFO");
    auto* hz = widget(lfo, "rateHz");
    auto* division = widget(lfo, "rateSync");
    const auto size = lfo.card->getBounds();
    const auto level = widget(lfo, "level")->getBounds();

    // Sync is on by default: the division shows and the Hz dial waits in the same cell.
    EXPECT_TRUE(division->isVisible());
    EXPECT_FALSE(hz->isVisible());
    EXPECT_TRUE(lfo.body->isSwappedOut(*hz));
    EXPECT_EQ(hz->getY(), division->getY());
    const auto hzTop = hz->getY();

    flip(canvas, lfo, "mode", 0.0f);
    EXPECT_TRUE(hz->isVisible());
    EXPECT_FALSE(division->isVisible());
    EXPECT_TRUE(lfo.body->isSwappedOut(*division));
    EXPECT_EQ(hz->getY(), hzTop);
    EXPECT_EQ(division->getY(), hzTop);
    EXPECT_EQ(lfo.card->getBounds(), size) << "a swap never resizes the card";
    EXPECT_EQ(widget(lfo, "level")->getBounds(), level) << "nor moves a neighbour";
    EXPECT_FALSE(lfo.body->hasMoreRow()) << "a swapped-out control never joins More";
}

TEST(SourcesDefaultLayout, OscillatorFixedRateSwapsWithGlideInOneCell) {
    CardCanvas canvas;
    const auto osc = build(canvas, "Oscillator");
    auto* glide = widget(osc, "glide");
    auto* fixedRate = widget(osc, "fixedRate");
    const auto size = osc.card->getBounds();
    const auto glideBounds = glide->getBounds();

    // Follow notes is the default: Glide shows and Fixed rate waits in its cell.
    EXPECT_TRUE(glide->isVisible());
    EXPECT_FALSE(fixedRate->isVisible());
    EXPECT_TRUE(osc.body->isSwappedOut(*fixedRate));

    flip(canvas, osc, "rateMode", 1.0f);
    EXPECT_TRUE(fixedRate->isVisible());
    EXPECT_FALSE(glide->isVisible());
    EXPECT_EQ(fixedRate->getY(), glideBounds.getY());
    EXPECT_EQ(fixedRate->getX(), glideBounds.getX());
    EXPECT_EQ(osc.card->getBounds(), size) << "a swap never resizes the card";
    EXPECT_TRUE(osc.body->isSwappedOut(*glide));
}

TEST(SourcesDefaultLayout, LfoGlideDimsUnlessTheShapeIsSampleAndHold) {
    CardCanvas canvas;
    const auto lfo = build(canvas, "LFO");
    const auto glide = widget(lfo, "glide")->getBounds();
    const auto size = lfo.card->getBounds();
    expectDimmedAndOperable(lfo, "glide", true);
    flip(canvas, lfo, "shape", 4.0f); // S&H
    expectDimmedAndOperable(lfo, "glide", false);
    flip(canvas, lfo, "shape", 3.0f); // Square
    expectDimmedAndOperable(lfo, "glide", true);
    EXPECT_EQ(widget(lfo, "glide")->getBounds(), glide);
    EXPECT_EQ(lfo.card->getBounds(), size);
}

// Picking Custom on the Shape combo still opens the card's custom-wave editor.
TEST(SourcesDefaultLayout, LfoCustomShapeStillOpensTheCurveEditor) {
    CardCanvas canvas;
    const auto lfo = build(canvas, "LFO");
    auto editorVisible = [&] {
        for (auto* child : lfo.card->getChildren())
            if (child->getTitle() == "LFO custom wave")
                return child->isVisible();
        ADD_FAILURE() << "no custom wave editor on the card";
        return false;
    };
    EXPECT_FALSE(editorVisible());
    const int height = lfo.card->getHeight();

    flip(canvas, lfo, "shape", 5.0f); // Custom
    EXPECT_TRUE(editorVisible());
    EXPECT_GT(lfo.card->getHeight(), height);

    flip(canvas, lfo, "shape", 0.0f);
    EXPECT_FALSE(editorVisible());
    EXPECT_EQ(lfo.card->getHeight(), height);
}

// A segmented switch gives each value an equal share of the 256 px body; the look-and-feel squeezes a
// name to 70% of its width before it ellipsises, so every value must fit at that scale.
TEST(SourcesDefaultLayout, EverySegmentedValueFitsItsSegment) {
    CardCanvas canvas;
    for (const auto& [type, paramId] : std::vector<std::pair<juce::String, juce::String>>{
             {"Oscillator", "waveform"}, {"Noise", "noiseType"}, {"Sampler", "playMode"}}) {
        SCOPED_TRACE(type.toStdString());
        const auto card = build(canvas, type);
        auto* shape = dynamic_cast<synth::ui::CardSegmentedSwitch*>(widget(card, paramId));
        ASSERT_NE(shape, nullptr);
        for (int i = 0; i < shape->getNumSegments(); ++i) {
            auto* segment = shape->getSegment(i);
            const juce::Font font(juce::FontOptions(15.0f));
            EXPECT_LE(font.getStringWidth(segment->getButtonText()) * 0.7f, (float)segment->getWidth() - 8.0f)
                << segment->getButtonText();
        }
    }
}

// ---- Every module of the family ----------------------------------------------------------------------

TEST(SourcesDefaultLayout, TheEstimateEqualsTheRealCardSize) {
    CardCanvas canvas;
    for (const juce::String type : {"Oscillator", "Noise", "Sampler", "Drum Kit", "Pluck", "Mallet", "LFO"}) {
        SCOPED_TRACE(type.toStdString());
        const auto card = build(canvas, type);
        const auto estimate = synth::measureDataDrivenCardSize(type);
        ASSERT_TRUE(estimate.has_value());
        EXPECT_EQ(*estimate, juce::Point<int>(card.card->getWidth(), card.card->getHeight()));
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(card.card->getWidth());
        EXPECT_EQ(card.body->layout(100, g, false), card.body->layout(100, g, true));
    }
}

TEST(SourcesDefaultLayout, EveryControlIsReachableNamedAndHasATooltip) {
    CardCanvas canvas;
    for (const juce::String type : {"Oscillator", "Noise", "Sampler", "Drum Kit", "Pluck", "Mallet", "LFO"}) {
        SCOPED_TRACE(type.toStdString());
        const auto card = build(canvas, type);
        const auto tabbable = card.card->createKeyboardFocusTraverser()->getAllComponents(card.card);
        for (const auto& item : card.body->getPlan().items) {
            ASSERT_NE(item.param, nullptr);
            SCOPED_TRACE(item.param->paramID.toStdString());
            auto* w = item.widget;
            ASSERT_NE(w, nullptr);
            EXPECT_TRUE(w->getWantsKeyboardFocus());
            EXPECT_TRUE(w->getTitle().isNotEmpty());
            auto* client = dynamic_cast<juce::SettableTooltipClient*>(w);
            ASSERT_NE(client, nullptr);
            EXPECT_TRUE(client->getTooltip().isNotEmpty());
            if (w->isVisible()) // a swapped-out or hidden-section control is not in the Tab order
                EXPECT_NE(std::find(tabbable.begin(), tabbable.end(), w), tabbable.end()) << "a Tab stop";
        }
        for (const auto& section : card.body->getPlan().sections)
            if (section.title.has_value())
                EXPECT_NE(*section.title, section.title->toUpperCase()) << "titles are sentence case";
    }
}

TEST(SourcesDefaultLayout, TheDefaultsOnlyNameParametersThatExistAndNeverPoly) {
    const auto& defaults = synth::DefaultCardLayouts::builtIn();
    for (const juce::String type :
         {"Oscillator", "Noise", "Sampler", "Drum Kit", "Pluck", "Mallet", "LFO", "Wavetable"}) {
        SCOPED_TRACE(type.toStdString());
        const auto* entry = defaults.find(type);
        ASSERT_NE(entry, nullptr);
        const int revision = type == "Sampler" ? 3 : (type == "Drum Kit" ? 2 : (type == "Oscillator" ? 3 : 1));
        EXPECT_EQ(entry->defaultRevision, revision);
        auto module = synth::AIStateMapper::createModule(type);
        for (const auto& section : entry->layout.sections)
            for (const auto& item : section.items)
                if (const auto* param = std::get_if<synth::CardParamItem>(&item)) {
                    EXPECT_NE(findParameterByID(module.get(), param->paramId), nullptr) << param->paramId;
                    EXPECT_NE(param->paramId, "poly");
                    if (param->when.has_value())
                        EXPECT_NE(findParameterByID(module.get(), param->when->param), nullptr);
                }
    }
}

// ---- Joined Left/Right jacks -------------------------------------------------------------------------
// Pan, and the unison Width, are disabled (dimmed, with a tooltip saying how to get them back) while a split-block
// module's jacks are joined, because the one jack carries a single mono leg. Splitting them, or a saved split module,
// has them enabled; effect modules keep their raw legs and are never affected.

namespace {

constexpr const char* kPanHint = "Split the Left/Right jacks to pan";
constexpr const char* kWidthHint = "Split the Left/Right jacks to spread the voices";

struct JoinedCard {
    NodeID id;
    ModuleComponent* card = nullptr;
    juce::AudioProcessor* module = nullptr;
};

JoinedCard buildJoined(CardCanvas& canvas, const juce::String& type, bool joined) {
    JoinedCard result;
    result.id = canvas.add(synth::AIStateMapper::createModule(type), 0, 0);
    result.module = canvas.processor(result.id);
    if (joined)
        findParameterByID(result.module, "dualIO")->setValueNotifyingHost(0.0f);
    canvas.editor.updateComponents();
    result.card = canvas.card(result.id);
    return result;
}

void setJoined(const JoinedCard& card, bool joined) {
    findParameterByID(card.module, "dualIO")->setValueNotifyingHost(joined ? 0.0f : 1.0f);
    card.card->getCardBody()->flushPendingConditionUpdate();
}

juce::Component* joinedWidget(const JoinedCard& card, const char* paramId) {
    return card.card->getCardBody()->findWidget(paramId);
}

void expectDisabled(const JoinedCard& card, const char* paramId, const char* hint) {
    SCOPED_TRACE(paramId);
    auto* w = joinedWidget(card, paramId);
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->isEnabled());
    EXPECT_TRUE(AppLookAndFeel::paintsDimmed(*w));
    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(w);
    ASSERT_NE(tip, nullptr);
    EXPECT_EQ(tip->getTooltip(), juce::String(hint));
    EXPECT_FALSE(w->getTitle().isEmpty()) << "keeps its accessibility name";
}

void expectEnabled(const JoinedCard& card, const char* paramId, const char* hint) {
    SCOPED_TRACE(paramId);
    auto* w = joinedWidget(card, paramId);
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->isEnabled());
    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(w);
    ASSERT_NE(tip, nullptr);
    EXPECT_NE(tip->getTooltip(), juce::String(hint));
    EXPECT_FALSE(w->getTitle().isEmpty());
}

} // namespace

TEST(JoinedJackDisable, OscillatorPanAndWidthFollowTheJackSplit) {
    CardCanvas canvas;
    const auto osc = buildJoined(canvas, "Oscillator", false);
    expectEnabled(osc, "pan", kPanHint);
    expectEnabled(osc, "width", kWidthHint);
    setJoined(osc, true);
    expectDisabled(osc, "pan", kPanHint);
    expectDisabled(osc, "width", kWidthHint);
    setJoined(osc, false);
    expectEnabled(osc, "pan", kPanHint);
    expectEnabled(osc, "width", kWidthHint);
}

TEST(JoinedJackDisable, WavetableAndFilterPanFollowTheJackSplit) {
    for (const char* type : {"Wavetable", "Filter"}) {
        SCOPED_TRACE(type);
        CardCanvas canvas;
        const auto card = buildJoined(canvas, type, false);
        setJoined(card, true);
        expectDisabled(card, "pan", kPanHint);
        setJoined(card, false);
        expectEnabled(card, "pan", kPanHint);
    }
}

TEST(JoinedJackDisable, ASavedJoinedModuleLoadsWithPanAndWidthDisabled) {
    CardCanvas canvas;
    const auto osc = buildJoined(canvas, "Oscillator", true);
    expectDisabled(osc, "pan", kPanHint);
    expectDisabled(osc, "width", kWidthHint);
}

// Regression test for FRO1130: a greyed Pan dropped out of the accessibility tree, because JUCE lists a container's
// children through a traverser that skips disabled components.
TEST(JoinedJackDisable, AGreyedPanStaysInTheAccessibilityTreeAsDisabled) {
    if (nativeWindowsAbortHere())
        GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
    CardCanvas canvas;
    const auto osc = buildJoined(canvas, "Oscillator", true);
    canvas.editor.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    canvas.editor.setVisible(true);
    auto* pan = joinedWidget(osc, "pan");
    ASSERT_NE(pan, nullptr);
    ASSERT_FALSE(pan->isEnabled());
    auto* handler = pan->getAccessibilityHandler();
    auto* cardHandler = osc.card->getAccessibilityHandler();
    if (handler == nullptr || cardHandler == nullptr) {
        canvas.editor.removeFromDesktop();
        GTEST_SKIP() << "this machine gave no accessibility handlers to a temporary window";
    }
    EXPECT_FALSE(handler->isIgnored());
    EXPECT_EQ(handler->getTitle(), juce::String("Pan"));
    EXPECT_FALSE(handler->getComponent().isEnabled()) << "the platform reports a disabled control from this";
    EXPECT_EQ(handler->getDescription(), juce::String(kPanHint));
    const auto kids = cardHandler->getChildren();
    EXPECT_NE(std::find(kids.begin(), kids.end(), handler), kids.end()) << "Pan is a child of its card in the tree";
    setJoined(osc, false);
    const auto after = cardHandler->getChildren();
    EXPECT_NE(std::find(after.begin(), after.end(), handler), after.end());
    canvas.editor.removeFromDesktop();
}

TEST(JoinedJackDisable, AnEffectModuleKeepsItsPanWhenJoined) {
    CardCanvas canvas;
    const auto delay = buildJoined(canvas, "Delay", true);
    auto* pan = joinedWidget(delay, "pan");
    ASSERT_NE(pan, nullptr);
    EXPECT_TRUE(pan->isEnabled());
    EXPECT_FALSE(AppLookAndFeel::paintsDimmed(*pan));
}
