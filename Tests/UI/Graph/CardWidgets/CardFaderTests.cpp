// CardFaderTests.cpp
//
// Source/UI/Graph/CardWidgets/CardFader: a module card's fader. On its own: a drag follows the mouse as
// one change gesture, Shift drags at an eighth without a jump, Cmd-click and double-click reset to the
// default, a right click does nothing to the value, and the keys step it. On a card: it is in the
// card's slider list, learnable, a modulation target a cable lands on, its modulation bar paints from
// the base value to base + CV in the theme's colour, and Alt-drag adjusts the routing's amount.

#include "../CardBody/CardBodyTestHelpers.h"
#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"

using namespace cardbody_test;
using synth::ui::CardFader;

namespace {

const juce::ModifierKeys kLeft(juce::ModifierKeys::leftButtonModifier);
const juce::ModifierKeys kShift(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);

struct GestureCounter : juce::Slider::Listener {
    int starts = 0, ends = 0;
    void sliderValueChanged(juce::Slider*) override {}
    void sliderDragStarted(juce::Slider*) override { ++starts; }
    void sliderDragEnded(juce::Slider*) override { ++ends; }
};

struct LoneFader {
    CardFader fader;
    GestureCounter gestures;

    explicit LoneFader(CardFader::Orientation orientation = CardFader::Orientation::Vertical)
        : fader(orientation) {
        fader.setRange(0.0, 1.0);
        fader.setDoubleClickReturnValue(true, 0.25);
        if (orientation == CardFader::Orientation::Vertical)
            fader.setSize(CardFader::kVerticalWidth, synth::cardbody::kFaderVHeight);
        else
            fader.setSize(240, synth::cardbody::kFaderHHeight);
        fader.setValue(0.5, juce::dontSendNotification);
        fader.addListener(&gestures);
    }

    juce::Point<float> cap() {
        const auto t = fader.travelBounds();
        const float pos = fader.positionForProportion(fader.valueToProportionOfLength(fader.getValue()));
        return fader.isVerticalFader() ? juce::Point<float>(t.getCentreX(), pos)
                                       : juce::Point<float>(pos, t.getCentreY());
    }

    float travelLength() {
        const auto t = fader.travelBounds();
        return fader.isVerticalFader() ? t.getHeight() : t.getWidth();
    }
};

ModulationTarget targetFor(juce::AudioProcessor& module, const juce::String& paramId) {
    for (const auto& t : dynamic_cast<ModuleBase&>(module).getModulationTargets())
        if (t.paramId == paramId)
            return t;
    return {};
}

// Feeds `cv` through the attenuverter once so the routing snapshot carries a live modulation value.
void driveAttenuverter(juce::AudioProcessor& atten, float cv) {
    atten.prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> buffer(2, 64);
    buffer.clear();
    for (int i = 0; i < 64; ++i)
        buffer.setSample(0, i, cv);
    juce::MidiBuffer midi;
    atten.processBlock(buffer, midi);
}

} // namespace

TEST(CardFader, DragUpMovesAVerticalFaderAsOneGesture) {
    LoneFader f;
    const auto start = f.cap();
    const float quarter = f.travelLength() * 0.25f;
    f.fader.mouseDown(mouseAt(f.fader, start, kLeft));
    f.fader.mouseDrag(mouseAt(f.fader, start - juce::Point<float>(0.0f, quarter), kLeft));
    f.fader.mouseUp(mouseAt(f.fader, start - juce::Point<float>(0.0f, quarter), kLeft));
    EXPECT_NEAR(f.fader.getValue(), 0.75, 0.01);
    EXPECT_EQ(f.gestures.starts, 1);
    EXPECT_EQ(f.gestures.ends, 1);
}

TEST(CardFader, HorizontalFaderFollowsTheMouseAcross) {
    LoneFader f(CardFader::Orientation::Horizontal);
    const auto start = f.cap();
    const float tenth = f.travelLength() * 0.1f;
    f.fader.mouseDown(mouseAt(f.fader, start, kLeft));
    f.fader.mouseDrag(mouseAt(f.fader, start - juce::Point<float>(tenth, 0.0f), kLeft));
    f.fader.mouseUp(mouseAt(f.fader, start - juce::Point<float>(tenth, 0.0f), kLeft));
    EXPECT_NEAR(f.fader.getValue(), 0.4, 0.01);
}

TEST(CardFader, ShiftDragMovesAtAnEighthAndTogglingShiftNeverJumps) {
    LoneFader f;
    const auto start = f.cap();
    const float half = f.travelLength() * 0.5f;
    f.fader.mouseDown(mouseAt(f.fader, start, kShift));
    f.fader.mouseDrag(mouseAt(f.fader, start - juce::Point<float>(0.0f, half), kShift));
    EXPECT_NEAR(f.fader.getValue(), 0.5 + 0.5 / 8.0, 0.005);

    // Releasing Shift where the mouse is changes nothing; only the rate changes from here.
    const double beforeToggle = f.fader.getValue();
    f.fader.mouseDrag(mouseAt(f.fader, start - juce::Point<float>(0.0f, half), kLeft));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), beforeToggle);
    f.fader.mouseUp(mouseAt(f.fader, start - juce::Point<float>(0.0f, half), kLeft));
    EXPECT_EQ(f.gestures.starts, 1);
}

TEST(CardFader, CmdClickAndDoubleClickResetToTheDefaultAsOneGestureEach) {
    LoneFader f;
    const juce::ModifierKeys cmd(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier);
    f.fader.mouseDown(mouseAt(f.fader, f.cap(), cmd));
    f.fader.mouseUp(mouseAt(f.fader, f.cap(), cmd));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), 0.25);
    EXPECT_EQ(f.gestures.starts, 1);

    f.fader.setValue(0.9, juce::dontSendNotification);
    f.fader.mouseDoubleClick(mouseAt(f.fader, f.cap(), kLeft, 2));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), 0.25);
    EXPECT_EQ(f.gestures.starts, 2);
    EXPECT_EQ(f.gestures.ends, 2);
}

// A real double-click arrives as down, up, down (2 clicks), double-click, up: the second press must
// not start a drag that the reset would then nest inside.
TEST(CardFader, ARealDoubleClickIsTwoBalancedGesturesEndingAtTheDefault) {
    LoneFader f;
    const auto at = f.cap();
    f.fader.mouseDown(mouseAt(f.fader, at, kLeft, 1));
    f.fader.mouseUp(mouseAt(f.fader, at, kLeft, 1));
    f.fader.mouseDown(mouseAt(f.fader, at, kLeft, 2));
    f.fader.mouseDoubleClick(mouseAt(f.fader, at, kLeft, 2));
    f.fader.mouseUp(mouseAt(f.fader, at, kLeft, 2));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), 0.25);
    EXPECT_EQ(f.gestures.starts, 2);
    EXPECT_EQ(f.gestures.ends, 2);
}

TEST(CardFader, RightClickNeitherMovesTheValueNorStartsAGesture) {
    LoneFader f;
    const juce::ModifierKeys right(juce::ModifierKeys::rightButtonModifier);
    f.fader.mouseDown(mouseAt(f.fader, f.cap(), right));
    f.fader.mouseDrag(mouseAt(f.fader, f.cap() - juce::Point<float>(0.0f, 30.0f), right));
    f.fader.mouseUp(mouseAt(f.fader, f.cap(), right));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), 0.5);
    EXPECT_EQ(f.gestures.starts, 0);
}

TEST(CardFader, ArrowsShiftHomeAndEndStepTheValue) {
    LoneFader f;
    EXPECT_TRUE(f.fader.keyPressed(juce::KeyPress(juce::KeyPress::upKey)));
    EXPECT_NEAR(f.fader.getValue(), 0.51, 1e-9);
    EXPECT_TRUE(f.fader.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0)));
    EXPECT_NEAR(f.fader.getValue(), 0.509, 1e-9);
    EXPECT_TRUE(f.fader.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), 1.0);
    EXPECT_TRUE(f.fader.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    EXPECT_DOUBLE_EQ(f.fader.getValue(), 0.0);
    EXPECT_FALSE(f.fader.keyPressed(juce::KeyPress(juce::KeyPress::tabKey))) << "Tab stays with the card";
    EXPECT_EQ(f.gestures.starts, 4) << "each key press is one change gesture";
}

// The design system's sizes: a card's vertical fader is the small one (18x7 cap), never the mixer's
// large one, and a horizontal fader the medium one (10x18 cap); both paint through the shared painter.
TEST(CardFader, PaintsTheDesignSystemFaderAtCardSizes) {
    synth::theme::AppLookAndFeel lnf;
    for (auto orientation : {CardFader::Orientation::Vertical, CardFader::Orientation::Horizontal}) {
        LoneFader f(orientation);
        f.fader.setLookAndFeel(&lnf);
        const auto m =
            synth::theme::fader::metricsFor(f.fader.isVerticalFader(), f.fader.getWidth(), f.fader.getHeight());
        EXPECT_FLOAT_EQ(m.capW, f.fader.isVerticalFader() ? 18.0f : 10.0f);
        EXPECT_FLOAT_EQ(m.capH, f.fader.isVerticalFader() ? 7.0f : 18.0f);

        juce::Image image(juce::Image::ARGB, f.fader.getWidth(), f.fader.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g(image);
            f.fader.paintEntireComponent(g, false);
        }
        const auto cap = f.cap().toInt();
        EXPECT_GT(image.getPixelAt(cap.x, cap.y).getAlpha(), 0) << "the cap is painted at the value";
        const auto corner = image.getPixelAt(0, 0);
        EXPECT_EQ(corner.getAlpha(), 0) << "nothing paints outside the fader's own look";
        f.fader.setLookAndFeel(nullptr);
    }
}

TEST(CardFader, OnACardItIsAModulationTargetLearnableAndInTheSliderList) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutWith(*filter, {{"cutoff", synth::CardWidget::FaderV}});
    const auto id = canvas.add(std::move(filter), 0, 0, layout);
    canvas.editor.updateComponents();
    auto* card = canvas.card(id);
    auto* fader = dynamic_cast<CardFader*>(card->getCardBody()->findWidget("cutoff"));
    ASSERT_NE(fader, nullptr);
    EXPECT_TRUE(fader->isVerticalFader());
    EXPECT_EQ(fader->getHeight(), synth::cardbody::kFaderVHeight);
    EXPECT_EQ(fader->getWidth(), CardFader::kVerticalWidth);

    auto* cutoff = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(canvas.processor(id), "cutoff"));
    EXPECT_EQ(card->findMidiLearnableParamForTest(fader), cutoff);
    EXPECT_TRUE(fader->wantsModAmountGesture && fader->onModAmountGesture && fader->wantsCablePickupGesture &&
                fader->onCablePickupGesture && fader->onHoverChanged);
    const auto target = targetFor(*canvas.processor(id), "cutoff");
    EXPECT_GE(card->sliderIndexForModTarget(target), 0) << "a modulation target resolves to the fader";
    auto* mb = dynamic_cast<ModuleBase*>(canvas.processor(id));
    EXPECT_TRUE(card->isInputJackKnobBound(mb->mapInputChannel(target.channelIndex).visibleJackIndex))
        << "the jack lands on the fader, as it would on a knob";
    const auto anchor = card->getModTargetKnobAnchor(target.channelIndex);
    ASSERT_TRUE(anchor.has_value());
    EXPECT_EQ(*anchor, fader->getPosition().toFloat() + fader->landingPoint(ModuleComponent::kKnobLandingDotDiameter));

    // Dragging the fader moves the parameter, and the card's height never changes while it does.
    const int height = card->getHeight();
    const auto t = fader->travelBounds();
    fader->mouseDown(mouseAt(*fader, t.getCentre(), kLeft));
    fader->mouseDrag(mouseAt(*fader, t.getCentre().translated(0.0f, -20.0f), kLeft));
    fader->mouseUp(mouseAt(*fader, t.getCentre().translated(0.0f, -20.0f), kLeft));
    EXPECT_NEAR(cutoff->convertFrom0to1(cutoff->getValue()), (float)fader->getValue(), 1.0f);
    EXPECT_EQ(card->getHeight(), height);
}

TEST(CardFader, ACableDroppedOnTheFaderLandsOnItsParameter) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutWith(*filter, {{"cutoff", synth::CardWidget::FaderH}});
    const auto lfoId = canvas.add(std::make_unique<LFOModule>(), 0, 0);
    const auto filterId = canvas.add(std::move(filter), 600, 0, layout);
    canvas.editor.updateComponents();
    auto* lfoCard = canvas.card(lfoId);
    auto* filterCard = canvas.card(filterId);
    lfoCard->setTopLeftPosition(0, 0);
    filterCard->setTopLeftPosition(600, 0);
    auto* fader = filterCard->getCardBody()->findWidget("cutoff");
    ASSERT_NE(dynamic_cast<CardFader*>(fader), nullptr);

    const auto onFader = filterCard->getPosition() + fader->getBounds().getCentre();
    canvas.editor.beginConnectionDrag(lfoCard, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});
    canvas.editor.dragConnection(onFader);
    canvas.editor.endConnectionDrag(onFader);

    const int cutoffChannel = targetFor(*canvas.processor(filterId), "cutoff").channelIndex;
    bool routed = false;
    for (const auto& routing : canvas.engine.getModulationRoutings())
        routed |= routing.sourceNodeID == lfoId && routing.destNodeID == filterId &&
                  routing.destChannelIndex == cutoffChannel;
    EXPECT_TRUE(routed) << "the cable landed on the Cutoff fader";
}

namespace {

// LFO -> attenuverter -> VCA gain, the VCA's Gain shown as a horizontal fader.
struct ModulatedFader {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID vcaId, attenId;
    ModuleComponent* card = nullptr;
    CardFader* fader = nullptr;

    ModulatedFader() {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        auto& graph = engine.getGraph();
        auto vca = std::make_unique<VCAModule>();
        const auto layout = automaticLayoutWith(*vca, {{"gain", synth::CardWidget::FaderH}});
        const auto lfoId = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        auto vcaNode = graph.addNode(std::move(vca));
        vcaId = vcaNode->nodeID;
        synth::setCardLayoutOverride(graph, nullptr, vcaId, layout);
        attenId = engine.addModRouting(lfoId, 0, vcaId, targetFor(*vcaNode->getProcessor(), "gain").channelIndex);
        editor->updateComponents();
        editor->timerCallback();
        card = findModuleComp(*editor, vcaNode->getProcessor());
        fader = card != nullptr ? dynamic_cast<CardFader*>(card->getCardBody()->findWidget("gain")) : nullptr;
    }

    ~ModulatedFader() { engine.shutdown(); }

    juce::AudioParameterFloat* amount() {
        auto* node = engine.getGraph().getNodeForId(attenId);
        return node != nullptr
                   ? dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"))
                   : nullptr;
    }
};

} // namespace

TEST(CardFader, AltDragOrADragOnTheBarAdjustsTheRoutingAmountNotTheValue) {
    ModulatedFader f;
    ASSERT_NE(f.fader, nullptr);
    auto* amount = f.amount();
    ASSERT_NE(amount, nullptr);
    const double value = f.fader->getValue();

    const auto t = f.fader->travelBounds();
    const juce::ModifierKeys alt(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
    float before = amount->get();
    f.fader->mouseDown(mouseAt(*f.fader, t.getCentre(), alt));
    f.fader->mouseDrag(mouseAt(*f.fader, t.getCentre().translated(0.0f, 40.0f), alt));
    f.fader->mouseUp(mouseAt(*f.fader, t.getCentre().translated(0.0f, 40.0f), alt));
    EXPECT_NE(f.amount()->get(), before) << "Alt-drag adjusts the routing";
    EXPECT_DOUBLE_EQ(f.fader->getValue(), value) << "the fader's own value never moves during it";

    before = f.amount()->get();
    const auto onBar = f.fader->modBarTrack().getCentre();
    f.fader->mouseDown(mouseAt(*f.fader, onBar, kLeft));
    f.fader->mouseDrag(mouseAt(*f.fader, onBar.translated(0.0f, 40.0f), kLeft));
    f.fader->mouseUp(mouseAt(*f.fader, onBar.translated(0.0f, 40.0f), kLeft));
    EXPECT_NE(f.amount()->get(), before) << "a drag started on the bar adjusts the routing";
    EXPECT_DOUBLE_EQ(f.fader->getValue(), value);

    // The cable's landing dot picks the cable up rather than moving anything.
    EXPECT_TRUE(f.fader->wantsCablePickupGesture(
        mouseAt(*f.fader, f.fader->landingPoint(ModuleComponent::kKnobLandingDotDiameter), kLeft)));
    EXPECT_FALSE(f.fader->wantsCablePickupGesture(mouseAt(*f.fader, t.getCentre(), kLeft)));
}

// The bar runs from the base value to base + CV, beside the slot, in mod-ring-positive (CV above the
// base) -- painted from the same routing snapshot the knob ring reads.
TEST(CardFader, ModulationBarPaintsFromTheBaseValueToBasePlusCv) {
    ModulatedFader f;
    ASSERT_NE(f.fader, nullptr);
    synth::theme::AppLookAndFeel lnf;
    f.card->setLookAndFeel(&lnf);
    f.fader->setValue(f.fader->getMinimum(), juce::sendNotificationSync);
    f.amount()->setValueNotifyingHost(f.amount()->convertTo0to1(1.0f));
    auto* atten = f.engine.getGraph().getNodeForId(f.attenId)->getProcessor();
    driveAttenuverter(*atten, 0.5f);
    f.editor->timerCallback();

    const auto barTrack = f.fader->modBarTrack().translated((float)f.fader->getX(), (float)f.fader->getY());
    const auto colour = lnf.getTheme().colors.modRingPositive;
    auto paintCard = [&] {
        juce::Image image(juce::Image::ARGB, f.card->getWidth(), f.card->getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        f.card->paint(g);
        return image;
    };
    auto near = [](juce::Colour a, juce::Colour b) {
        return std::abs(a.getRed() - b.getRed()) < 40 && std::abs(a.getGreen() - b.getGreen()) < 40 &&
               std::abs(a.getBlue() - b.getBlue()) < 40;
    };

    const auto image = paintCard();
    const int y = juce::roundToInt(barTrack.getCentreY());
    const auto inBar = image.getPixelAt(juce::roundToInt(barTrack.getX() + barTrack.getWidth() * 0.2f), y);
    const auto pastBar = image.getPixelAt(juce::roundToInt(barTrack.getX() + barTrack.getWidth() * 0.9f), y);
    EXPECT_TRUE(near(inBar, colour)) << "from the base (zero) up to base + 0.5: " << inBar.toDisplayString(true);
    EXPECT_FALSE(near(pastBar, colour)) << "nothing beyond base + CV";

    // No CV: no bar.
    driveAttenuverter(*atten, 0.0f);
    f.editor->timerCallback();
    const auto still = paintCard().getPixelAt(juce::roundToInt(barTrack.getX() + barTrack.getWidth() * 0.2f), y);
    EXPECT_FALSE(near(still, colour));
    f.card->setLookAndFeel(nullptr);
}

TEST(CardFader, RightClickOffersAutomateAndMidiLearnForTheFader) {
    CardCanvas canvas;
    auto filter = std::make_unique<FilterModule>();
    const auto layout = automaticLayoutWith(*filter, {{"cutoff", synth::CardWidget::FaderV}});
    const auto id = canvas.add(std::move(filter), 0, 0, layout);
    canvas.editor.updateComponents();
    juce::String armed;
    canvas.editor.onMidiLearnRequested = [&armed](juce::AudioProcessorGraph::NodeID, const juce::String& paramId) {
        armed = paramId;
    };
    auto* card = canvas.card(id);
    auto* fader = card->getCardBody()->findWidget("cutoff");
    const double before = dynamic_cast<juce::Slider*>(fader)->getValue();

    const auto menu = rightClickOnCard(*card, *fader);
    const auto* learn = menuItem(menu, "MIDI Learn 'Cutoff'...");
    ASSERT_NE(learn, nullptr);
    EXPECT_NE(menuItem(menu, "Automate 'Cutoff'"), nullptr) << "the fader is in the card's slider list";
    learn->action();
    EXPECT_EQ(armed, "cutoff");
    EXPECT_DOUBLE_EQ(dynamic_cast<juce::Slider*>(fader)->getValue(), before) << "a right click never moves it";
}
