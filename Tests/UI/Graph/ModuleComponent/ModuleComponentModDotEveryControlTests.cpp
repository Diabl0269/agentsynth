// The mod dot on every control, through the real component hierarchy. Every module type's default card layout is
// built, every control that can carry a modulation (a rotary knob, a horizontal or vertical fader, a map slider) gets
// a routing, and a real press at the painted dot is delivered to whichever component sits under it on the canvas.
// The dot gesture must claim that press and open the dot's panel. This is the guard for the bug kind "a control
// kind whose dot nobody wired": a new widget kind with a dot that cannot be pressed fails here.

#include "ModuleComponentTestFixture.h"

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ModCableExemptions.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardWidgets/CardFader.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/ModDotButton.h"
#include "UI/Graph/ModDot/ModDotController.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <set>

namespace {

// Module types with no ModuleComponent card to press (graph plumbing), or no continuous input to modulate.
const std::set<juce::String> kNoModulatedControls = {"Macros",         "Macro In",    "Macro Out",     "Macro Midi In",
                                                     "Macro Midi Out", "Audio Input", "Audio Output",  "Midi Input",
                                                     "MIDI Keyboard",  "Poly MIDI",   "External MIDI", "Track In",
                                                     "Track Audio",    "Rec Tap",     "Hosted Plugin"};

const juce::RangedAudioParameter* continuousParamNamed(const ModuleBase& mb, const juce::String& displayName) {
    for (auto* p : mb.getParameters())
        if (auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(p);
            ranged != nullptr && ranged->getName(100) == displayName &&
            (dynamic_cast<const juce::AudioParameterFloat*>(p) != nullptr ||
             dynamic_cast<const juce::AudioParameterInt*>(p) != nullptr))
            return ranged;
    return nullptr;
}

// Every knob and fader the card holds, shown or not: one in a folded More row, a mode-hidden section or a tab is a
// control the person can reach, so it counts. Enumerates from the widgets, never from the modulation targets.
void collectSliders(juce::Component& parent, std::vector<juce::Slider*>& out) {
    for (auto* child : parent.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            out.push_back(slider);
        collectSliders(*child, out);
    }
}

juce::String widgetKind(const juce::Component& control) {
    if (const auto* fader = dynamic_cast<const synth::ui::CardFader*>(&control))
        return fader->isVerticalFader() ? "vertical fader" : "horizontal fader";
    if (const auto* slider = dynamic_cast<const juce::Slider*>(&control))
        return slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag ? "knob" : "other slider";
    return "other control";
}

// One control with a painted dot on a default card, as the walk below hands it over.
struct DotControl {
    juce::String typeName, targetName, kind, geometry;
    ModuleComponent* card = nullptr;
    GraphEditor* editor = nullptr;
    juce::Component* hit = nullptr; // what sits under the dot on the canvas; null when nothing does
    juce::Point<int> local;         // the dot, in `hit`'s coordinates
    int channel = 0;
    juce::AudioProcessorGraph::NodeID node;
    juce::Point<float> anchor;
};

// Builds every module type's default card, routes an LFO to every modulation target, and calls `visit` for each
// control that paints a dot, with the component a press at that dot would reach.
void forEachDotControl(const std::function<void(const DotControl&)>& visit) {
    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        if (kNoModulatedControls.count(typeName) > 0)
            continue;
        AudioEngine engine;
        engine.initialise();
        engine.getGraph().clear();
        {
            GraphEditor editor(engine);
            editor.setSize(1600, 1200);
            auto& graph = engine.getGraph();
            auto moduleNode = graph.addNode(synth::AIStateMapper::createModule(typeName));
            auto* mb = moduleNode != nullptr ? dynamic_cast<ModuleBase*>(moduleNode->getProcessor()) : nullptr;
            if (mb == nullptr)
                continue;
            moduleNode->properties.set("x", 300);
            moduleNode->properties.set("y", 100);
            auto lfo = graph.addNode(std::make_unique<LFOModule>());
            lfo->properties.set("x", 20);
            lfo->properties.set("y", 100);
            for (const auto& target : mb->getModulationTargets())
                engine.addModRouting(lfo->nodeID, 0, moduleNode->nodeID, target.channelIndex);
            editor.updateComponents();
            // The cards keep the size their layout asked for (no fixed test size): controls sit where a person sees
            // them.
            editor.timerCallback(); // counts the sources on every control, which creates the dot buttons

            auto* card = findModuleComp(editor, mb);
            auto* content = editor.getChildComponent(0);
            if (card == nullptr)
                continue; // a module drawn without a card (the Attenuverter's knob is the cable's midpoint)
            ASSERT_NE(content, nullptr);

            for (const auto& target : mb->getModulationTargets()) {
                auto* button = card->getModDotButton(target.channelIndex);
                const auto anchor = card->getModTargetKnobAnchor(target.channelIndex);
                if (button == nullptr || button->getKnob() == nullptr || !anchor.has_value() ||
                    button->getKnob()->getBounds().isEmpty() || !button->getKnob()->isVisible())
                    continue; // no dot is drawn: a hidden page, a tabbed knob, a control the card does not show
                DotControl dot;
                dot.typeName = typeName;
                dot.targetName = target.name;
                dot.kind = widgetKind(*button->getKnob());
                dot.card = card;
                dot.editor = &editor;
                dot.channel = target.channelIndex;
                dot.node = moduleNode->nodeID;
                dot.anchor = *anchor;
                const auto where = content->getLocalPoint(card, anchor->roundToInt());
                dot.hit = content->getComponentAt(where);
                if (dot.hit != nullptr)
                    dot.local = dot.hit->getLocalPoint(content, where);
                dot.geometry = juce::String(" dot ") + anchor->toString() + " control " +
                               button->getKnob()->getBounds().toString() + " card " + card->getLocalBounds().toString();
                visit(dot);
            }
        }
        engine.shutdown();
    }
}

// What a press at the dot sets off: the control's own handler, then the card's listener on that control (a plain
// juce::Component is its own MouseListener; JUCE's dispatch runs both for a real click).
void pressAt(const DotControl& dot, juce::ModifierKeys mods) {
    const auto event = makeModuleClickWithMods(*dot.hit, dot.local, mods);
    dot.hit->mouseDown(event);
    if (dynamic_cast<juce::Slider*>(dot.hit) != nullptr)
        dot.card->mouseDown(event);
    dot.hit->mouseUp(makeModuleClickWithMods(*dot.hit, dot.local, mods));
}

} // namespace

TEST_F(ModuleComponentTest, APressOnTheDotOfEveryModulatedControlOnEveryDefaultCardOpensItsPanel) {
    // The app's look: a fader's travel starts where the painter puts it, which decides where its dot sits.
    synth::theme::AppLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    const struct Restore {
        ~Restore() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    } restore;
    std::map<juce::String, int> pressedPerKind;
    std::vector<juce::String> unclaimed;

    forEachDotControl([&](const DotControl& dot) {
        if (dot.hit == nullptr) {
            unclaimed.push_back(dot.typeName + " / " + dot.targetName + " (" + dot.kind + "): nothing under the dot," +
                                dot.geometry);
            return;
        }
        std::vector<int> clicked; // the channel the dot's click hook reported
        dot.editor->getModDot().onModDotClicked = [&](juce::AudioProcessorGraph::NodeID node, int channel,
                                                      juce::Component&) {
            if (node == dot.node)
                clicked.push_back(channel);
        };
        dot.hit->mouseDown(makeModuleClickWithMods(*dot.hit, dot.local, plainLeftClick()));
        dot.hit->mouseUp(makeModuleClickWithMods(*dot.hit, dot.local, plainLeftClick()));

        // Two modes of one card may share a cell (the Sampler's swapped knobs): the press then reaches the
        // control on top, and the dot it opens is the one painted at the same spot.
        const bool sharesTheSpot =
            clicked.size() == 1 && dot.card->getModTargetKnobAnchor(clicked.front()).has_value() &&
            dot.card->getModTargetKnobAnchor(clicked.front())->getDistanceFrom(dot.anchor) < 1.0f;
        if (clicked.size() == 1 && (clicked.front() == dot.channel || sharesTheSpot))
            ++pressedPerKind[dot.kind];
        else
            unclaimed.push_back(dot.typeName + " / " + dot.targetName + " (" + dot.kind + ", pressed " +
                                juce::String(typeid(*dot.hit).name()) + ")," + dot.geometry + ", clicks " +
                                juce::String((int)clicked.size()));
    });

    EXPECT_GT(pressedPerKind["knob"], 0) << "the sweep must reach knobs";
    EXPECT_GT(pressedPerKind["horizontal fader"], 0) << "the sweep must reach horizontal faders";
    EXPECT_GT(pressedPerKind["vertical fader"], 0) << "the sweep must reach vertical faders";
    EXPECT_TRUE(unclaimed.empty()) << "presses on a painted mod dot that no gesture claimed:\n"
                                   << juce::StringArray(unclaimed.data(), (int)unclaimed.size()).joinIntoString("\n");
}

// Guard for the bug kind "one press claimed by two gestures": a right press (and a Ctrl-click, which macOS reports as
// one) on a painted dot belongs to the card's context menu alone, and a left press to the dot's panel alone.
TEST_F(ModuleComponentTest, ARightPressOnTheDotOfEveryModulatedControlOpensOneMenuAndNoPanel) {
    synth::theme::AppLookAndFeel lookAndFeel;
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    const struct Restore {
        ~Restore() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
    } restore;
    const juce::ModifierKeys rightButton(juce::ModifierKeys::rightButtonModifier);
    const juce::ModifierKeys ctrlClick(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier);
    std::vector<juce::String> wrong;
    int rightPresses = 0, leftPresses = 0;

    forEachDotControl([&](const DotControl& dot) {
        if (dot.hit == nullptr)
            return;
        int panels = 0, menus = 0;
        dot.editor->getModDot().onModDotClicked = [&](juce::AudioProcessorGraph::NodeID, int, juce::Component&) {
            ++panels;
        };
        dot.card->setShowContextMenuHookForTest([&](juce::PopupMenu&) { ++menus; });
        const auto check = [&](const char* what, juce::ModifierKeys mods, int wantMenus, int wantPanels) {
            panels = menus = 0;
            pressAt(dot, mods);
            if (menus != wantMenus || panels != wantPanels)
                wrong.push_back(dot.typeName + " / " + dot.targetName + " (" + dot.kind + ") " + what + ": " +
                                juce::String(menus) + " menus, " + juce::String(panels) + " panels");
        };
        // Only a control the card's context menu reaches is asked for its menu: a morph strip has its own.
        const bool cardMenuReachesIt = dynamic_cast<juce::Slider*>(dot.hit) != nullptr;
        check("right press", rightButton, cardMenuReachesIt ? 1 : 0, 0);
        // Only macOS reports a Ctrl-click as a popup-menu press; elsewhere Ctrl is the control's own drag modifier.
        if (ctrlClick.isPopupMenu())
            check("ctrl press", ctrlClick, cardMenuReachesIt ? 1 : 0, 0);
        check("left press", juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 1);
        rightPresses += cardMenuReachesIt ? 1 : 0;
        ++leftPresses;
        dot.card->setShowContextMenuHookForTest(nullptr);
    });

    EXPECT_GT(rightPresses, 50) << "the sweep must reach the cards' knobs and faders";
    EXPECT_GT(leftPresses, rightPresses - 1);
    EXPECT_TRUE(wrong.empty()) << "presses on a painted mod dot that did not open exactly the expected pop-ups:\n"
                               << juce::StringArray(wrong.data(), (int)wrong.size()).joinIntoString("\n");
}

// The converse of the test above: that one presses the dots that exist, so a control with no modulation target never
// entered it. This one starts from the widgets, so a new knob or fader with no target fails here.
bool isContinuous(const juce::AudioProcessorParameter* param) {
    return dynamic_cast<const juce::AudioParameterFloat*>(param) != nullptr ||
           dynamic_cast<const juce::AudioParameterInt*>(param) != nullptr;
}

// The Drum Kit's group knobs (kickTune, closedDecay ...) have no widget on the card: the pads area edits the pads, and
// the AI and automation write the groups. A widget walk cannot reach them, so each continuous parameter the card shows
// no knob for is checked by itself.
void checkParametersWithoutAWidget(const juce::String& typeName, const ModuleBase& mb,
                                   const std::set<const juce::RangedAudioParameter*>& onCard,
                                   const std::set<const juce::RangedAudioParameter*>& targeted,
                                   std::vector<juce::String>& gaps, int& checked) {
    if (typeName != "Drum Kit")
        return;
    for (auto* p : mb.getParameters()) {
        const auto* ranged = dynamic_cast<const juce::RangedAudioParameter*>(p);
        if (ranged == nullptr || !isContinuous(p) || onCard.count(ranged) > 0)
            continue;
        ++checked;
        if (targeted.count(ranged) == 0 && !modcable_test::slotHostTakesCable(mb, ranged->paramID) &&
            modcable_test::exemptionReason(typeName, ranged->paramID) == nullptr)
            gaps.push_back(typeName + " / " + ranged->paramID + " (\"" + ranged->getName(100) +
                           "\", no widget on the card)");
    }
}

TEST_F(ModuleComponentTest, EveryExemptionFromAModCableNamesItsReason) {
    for (const auto& e : modcable_test::exemptions()) {
        EXPECT_FALSE(e.reason.empty()) << e.type << " / " << e.paramId << " is exempt without a reason";
        EXPECT_FALSE(e.type.empty() || e.paramId.empty());
    }
}

TEST_F(ModuleComponentTest, EveryKnobAndFaderOnEveryDefaultCardTakesAModCableOrIsAnExplicitExemption) {
    std::vector<juce::String> gaps, staleExemptions;
    std::set<std::pair<juce::String, juce::String>> exemptionsSeen;
    int controlsChecked = 0, parametersChecked = 0;

    for (const auto& typeName : synth::AIStateMapper::moduleFactoryTypeNames()) {
        auto processor = synth::AIStateMapper::createModule(typeName);
        auto* mb = dynamic_cast<ModuleBase*>(processor.get());
        if (mb == nullptr)
            continue; // a graph-IO node, not a module card
        AudioEngine engine;
        GraphEditor editor(engine);
        ModuleComponent card(processor.get(), juce::AudioProcessorGraph::NodeID(1), editor);

        std::set<const juce::RangedAudioParameter*> targeted, onCard;
        for (const auto& target : mb->getModulationTargets())
            if (const auto* param = mb->parameterForModTarget(target))
                targeted.insert(param);

        std::vector<juce::Slider*> sliders;
        collectSliders(card, sliders);
        std::set<juce::String> reported;
        for (auto* slider : sliders) {
            // A knob that re-binds (the Drum Kit's picked-pad knobs) is found by what it drives; every other one by the
            // display name it carries as its component id.
            const juce::RangedAudioParameter* param = card.findMidiLearnableParamForTest(slider);
            if (param == nullptr || !isContinuous(param))
                param = continuousParamNamed(*mb, slider->getComponentID());
            if (param == nullptr)
                continue; // bound to no continuous parameter of this module: a picker, or a value of its own
            onCard.insert(param);
            ++controlsChecked;
            if (const auto* reason = modcable_test::exemptionReason(typeName, param->paramID)) {
                exemptionsSeen.insert({typeName, param->paramID});
                if (targeted.count(param) > 0)
                    staleExemptions.push_back(typeName + " / " + param->paramID + " has a target now (" + *reason +
                                              ")");
                continue;
            }
            if (targeted.count(param) == 0 && !modcable_test::slotHostTakesCable(*mb, param->paramID) &&
                reported.insert(param->paramID).second)
                gaps.push_back(typeName + " / " + param->paramID + " (\"" + param->getName(100) + "\", " +
                               widgetKind(*slider) + (slider->isVisible() ? ", shown" : ", hidden") + ")");
        }
        checkParametersWithoutAWidget(typeName, *mb, onCard, targeted, gaps, parametersChecked);
    }
    for (const auto& e : modcable_test::exemptions())
        if (e.onCardAsSlider && exemptionsSeen.count({e.type, e.paramId}) == 0)
            staleExemptions.push_back(juce::String(e.type) + " / " + e.paramId + " is no longer on the card");

    EXPECT_GT(controlsChecked, 100) << "the sweep must reach the cards' controls";
    EXPECT_GT(parametersChecked, 20) << "the sweep must reach the Drum Kit's group knobs";
    EXPECT_TRUE(gaps.empty()) << "knobs and faders that refuse a modulation cable:\n"
                              << juce::StringArray(gaps.data(), (int)gaps.size()).joinIntoString("\n");
    EXPECT_TRUE(staleExemptions.empty())
        << "stale exemptions:\n"
        << juce::StringArray(staleExemptions.data(), (int)staleExemptions.size()).joinIntoString("\n");
}
