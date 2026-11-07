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

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/CardLayout.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/ModuleViews/FrequencyResponseComponent.h"
#include "UI/ModuleViews/ScopeComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeelFader.h"
#include <gtest/gtest.h>
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
    return !isBodyControl(c, parts) && !hasOwnSpacing(c);
}

void checkCard(ModuleComponent& card, const juce::String& context) {
    std::vector<juce::Component*> visible;
    for (auto* child : card.getChildren())
        if (child->isVisible() && !child->getBounds().isEmpty() &&
            !juce::String(typeid(*child).name()).contains("PortAccessible")) // jacks sit on the card edge by design
            visible.push_back(child);

    for (size_t i = 0; i < visible.size(); ++i)
        for (size_t j = i + 1; j < visible.size(); ++j)
            EXPECT_FALSE(visible[i]->getBounds().intersects(visible[j]->getBounds()))
                << context << "\n  " << name(*visible[i]) << "\n  overlaps " << name(*visible[j]);

    const auto parts = bodyParts(card);
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

            ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
            for (auto* child : card.getChildren())
                if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child);
                    toggle != nullptr && toggle->getButtonText() == synth::cardbody::kShowEnvelopeText)
                    toggle->setToggleState(state.envelopeGraph, juce::sendNotificationSync);
            checkCard(card, type + ": " + state.describe());
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
    for (const auto& type : types) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (dynamic_cast<ModuleBase*>(processor.get()) == nullptr)
            continue;
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);
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
    }
    EXPECT_GT(checked, 0);
}
