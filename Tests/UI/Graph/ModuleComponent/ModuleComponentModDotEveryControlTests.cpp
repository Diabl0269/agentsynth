// The mod dot on every control, through the real component hierarchy. Every module type's default card layout is
// built, every control that can carry a modulation (a rotary knob, a horizontal or vertical fader, a map slider) gets
// a routing, and a real press at the painted dot is delivered to whichever component sits under it on the canvas.
// The dot gesture must claim that press and open the dot's panel. This is the guard for the bug kind "a control
// kind whose dot nobody wired": a new widget kind with a dot that cannot be pressed fails here.

#include "ModuleComponentTestFixture.h"

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/LFOModule.h"
#include "Modules/ModuleBase.h"
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

juce::String widgetKind(const juce::Component& control) {
    if (const auto* fader = dynamic_cast<const synth::ui::CardFader*>(&control))
        return fader->isVerticalFader() ? "vertical fader" : "horizontal fader";
    if (const auto* slider = dynamic_cast<const juce::Slider*>(&control))
        return slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag ? "knob" : "other slider";
    return "other control";
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

            std::vector<std::pair<int, juce::String>> clicked; // (channel, node) the dot's click hook reported
            editor.getModDot().onModDotClicked = [&](juce::AudioProcessorGraph::NodeID node, int channel,
                                                     juce::Component&) {
                if (node == moduleNode->nodeID)
                    clicked.emplace_back(channel, juce::String());
            };

            for (const auto& target : mb->getModulationTargets()) {
                auto* button = card->getModDotButton(target.channelIndex);
                const auto anchor = card->getModTargetKnobAnchor(target.channelIndex);
                if (button == nullptr || button->getKnob() == nullptr || !anchor.has_value() ||
                    button->getKnob()->getBounds().isEmpty() || !button->getKnob()->isVisible())
                    continue; // no dot is drawn: a hidden page, a tabbed knob, a control the card does not show
                const auto kind = widgetKind(*button->getKnob());
                const auto where = content->getLocalPoint(card, anchor->roundToInt());
                auto* hit = content->getComponentAt(where);
                const auto geometry = juce::String(" dot ") + anchor->toString() + " control " +
                                      button->getKnob()->getBounds().toString() + " card " +
                                      card->getLocalBounds().toString();
                if (hit == nullptr) {
                    unclaimed.push_back(typeName + " / " + target.name + " (" + kind + "): nothing under the dot," +
                                        geometry);
                    continue;
                }

                clicked.clear();
                const auto local = hit->getLocalPoint(content, where);
                hit->mouseDown(makeModuleClickWithMods(*hit, local, plainLeftClick()));
                hit->mouseUp(makeModuleClickWithMods(*hit, local, plainLeftClick()));

                // Two modes of one card may share a cell (the Sampler's swapped knobs): the press then reaches the
                // control on top, and the dot it opens is the one painted at the same spot.
                const bool sharesTheSpot =
                    clicked.size() == 1 && card->getModTargetKnobAnchor(clicked.front().first).has_value() &&
                    card->getModTargetKnobAnchor(clicked.front().first)->getDistanceFrom(*anchor) < 1.0f;
                if (clicked.size() == 1 && (clicked.front().first == target.channelIndex || sharesTheSpot))
                    ++pressedPerKind[kind];
                else
                    unclaimed.push_back(typeName + " / " + target.name + " (" + kind + ", pressed " +
                                        juce::String(typeid(*hit).name()) + ")," + geometry + ", clicks " +
                                        juce::String((int)clicked.size()));
            }
        }
        engine.shutdown();
    }

    EXPECT_GT(pressedPerKind["knob"], 0) << "the sweep must reach knobs";
    EXPECT_GT(pressedPerKind["horizontal fader"], 0) << "the sweep must reach horizontal faders";
    EXPECT_GT(pressedPerKind["vertical fader"], 0) << "the sweep must reach vertical faders";
    EXPECT_TRUE(unclaimed.empty()) << "presses on a painted mod dot that no gesture claimed:\n"
                                   << juce::StringArray(unclaimed.data(), (int)unclaimed.size()).joinIntoString("\n");
}
