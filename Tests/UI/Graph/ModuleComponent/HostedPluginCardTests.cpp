// HostedPluginCardTests.cpp -- the Hosted Plugin card body (FRO128): the knobs / toggles / choice combos the
// resolved layout puts on the card, HostedParameterAttachment's two-way binding, the layout-change rebuilds,
// gesture -> undo bracketing, and the unbind discipline when the instance goes away.
//
// Everything runs against Tests/StubPluginInstance.h (no real plugin binary can live in this repo). The card
// is built the way ModuleComponentEnvelopeCardTests builds one: a real node in the engine's graph plus a
// ModuleComponent on the stack, so graph snapshots and detachFromProcessor() see a real node.
//
// Groups:
//   1. Which widget each slot kind renders, labels, empty layout, orphan slots.
//   2. Where the layout comes from: automatic default, instance override, stored default; rebuild triggers.
//   3. HostedParameterAttachment: value/text round trips, threading, no echo, gestures, detach.
//   4. Undo: one gesture pair = one undo step.
//   5. Lifetime: instance retired / replaced / node deleted / card detached after its node was freed.
//   6. PNG render hook (PLUGIN_CARD_PNG).

#include "../../../StubPluginInstance.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/CardLayout.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/HostedParameterAttachment.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

using synth::HostedPluginModule;
using synth::test::StubBackend;
using synth::test::StubParamSpec;
using synth::test::StubParamTraits;
using synth::test::StubPluginInstance;
using synth::ui::HostedParameterAttachment;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

/** Delivers everything already posted to the message loop (an AsyncUpdater, a callAsync). */
void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

juce::PluginDescription stubDescription() {
    juce::PluginDescription description;
    description.name = "Card Plugin";
    description.pluginFormatName = "VST3";
    description.uniqueId = 0xC0DE01;
    description.deprecatedUid = 0xC0DE01;
    description.fileOrIdentifier = "/nonexistent/test/path/CardPlugin.vst3";
    return description;
}

StubParamSpec knobSpec(const juce::String& id, const juce::String& name, float def = 0.0f) {
    return {id, name, def, {}, false};
}
StubParamSpec toggleSpec(const juce::String& id, const juce::String& name) {
    StubParamTraits traits;
    traits.boolean = true;
    return {id, name, 0.0f, traits, false};
}
StubParamSpec choiceSpec(const juce::String& id, const juce::String& name, juce::StringArray choices) {
    StubParamTraits traits;
    traits.choices = std::move(choices);
    return {id, name, 0.0f, traits, false};
}

/** A stub whose saved state changes with its parameter values, like a real plugin's does. Without it a
 *  parameter move leaves the graph snapshot identical and no undo step is ever pushed. */
class StateTrackingStub : public StubPluginInstance {
public:
    using StubPluginInstance::StubPluginInstance;
    void getStateInformation(juce::MemoryBlock& destData) override {
        juce::String state;
        for (auto* param : getParameters())
            state << juce::String(param->getValue(), 4) << ",";
        destData.reset();
        destData.append(state.toRawUTF8(), state.getNumBytesAsUTF8());
    }
};

/** Counts gestures on one parameter, standing in for "the plugin / the host saw a begin and an end". */
struct GestureProbe : juce::AudioProcessorParameter::Listener {
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { (starting ? begins : ends)++; }
    int begins = 0;
    int ends = 0;
};

juce::MouseEvent mouseEvent(juce::Component& comp, juce::Point<float> pos, bool dragged, juce::Point<float> downPos) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1,
                            dragged);
}

/** A real press-drag-release on a rotary: the path that fires sliderDragStarted / sliderDragEnded. */
void dragSlider(juce::Slider& slider, float upwardPixels) {
    const auto start = slider.getLocalBounds().getCentre().toFloat();
    const auto end = start - juce::Point<float>(0.0f, upwardPixels);
    slider.mouseDown(mouseEvent(slider, start, false, start));
    slider.mouseDrag(mouseEvent(slider, end, true, start));
    slider.mouseUp(mouseEvent(slider, end, true, start));
}

template <typename T>
std::vector<T*> childrenOfType(juce::Component& parent) {
    std::vector<T*> found;
    for (auto* child : parent.getChildren())
        if (auto* typed = dynamic_cast<T*>(child))
            found.push_back(typed);
    return found;
}

template <typename T>
T* childWithId(juce::Component& parent, const juce::String& id) {
    for (auto* child : parent.getChildren())
        if (child->getComponentID() == id)
            if (auto* typed = dynamic_cast<T*>(child))
                return typed;
    return nullptr;
}

juce::Label* labelWithText(juce::Component& parent, const juce::String& text) {
    for (auto* label : childrenOfType<juce::Label>(parent))
        if (label->getText() == text)
            return label;
    return nullptr;
}

/** A hosted plugin as a node of a real engine graph, plus what a card needs around it. */
struct Rig {
    Rig()
        : tempDir(juce::File::getSpecialLocation(juce::File::tempDirectory)
                      .getChildFile("fro128-card-store-" + juce::Uuid().toString()))
        , store(tempDir)
        , editor(engine) {
        editor.setPluginCardLayoutStore(&store);
    }

    ~Rig() {
        editor.detachAllModuleComponents();
        tempDir.deleteRecursively();
    }

    /** Adds a Hosted Plugin node. `waitForInstance` false leaves the load in flight. */
    HostedPluginModule* addPlugin(std::vector<StubParamSpec> specs, bool waitForInstance = true,
                                  bool trackState = false) {
        backend.setFactory([specs, trackState]() -> std::unique_ptr<StubPluginInstance> {
            if (trackState)
                return std::make_unique<StateTrackingStub>(2, 2, "Card Plugin", 0xC0DE01, "VST3", specs);
            return std::make_unique<StubPluginInstance>(2, 2, "Card Plugin", 0xC0DE01, "VST3", specs);
        });

        auto* hosted = new HostedPluginModule();
        auto node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(hosted));
        module = hosted;
        nodeId = node->nodeID;
        hosted->prepareToPlay(kSampleRate, kBlockSize);
        hosted->loadPlugin(stubDescription(), backend);
        if (waitForInstance)
            EXPECT_TRUE(pumpUntil([hosted] { return hosted->hasInstance(); }));
        return hosted;
    }

    std::unique_ptr<ModuleComponent> makeCard(AppUndoManager* undo = nullptr) {
        return std::make_unique<ModuleComponent>(module, nodeId, editor, undo);
    }

    juce::AudioProcessorParameter* param(int index) const {
        return module->getActiveInstanceForEditor()->getParameters()[index];
    }

    juce::File tempDir;
    synth::PluginCardLayoutStore store; // declared before the editor: every card holds a listener on it
    AudioEngine engine;
    GraphEditor editor;
    StubBackend backend;
    HostedPluginModule* module = nullptr;
    juce::AudioProcessorGraph::NodeID nodeId;
};

synth::CardLayout layoutOf(std::initializer_list<synth::CardSlot> slots) {
    synth::CardLayout layout;
    layout.slots.assign(slots.begin(), slots.end());
    return layout;
}

synth::CardSlot slotFor(const juce::String& id, int hint, synth::CardSlotKind kind = synth::CardSlotKind::Auto,
                        std::optional<juce::String> label = std::nullopt) {
    synth::CardSlot slot;
    slot.paramId = id;
    slot.indexHint = hint;
    slot.kind = kind;
    slot.label = std::move(label);
    return slot;
}

} // namespace

// ============================================================================
// 1. Slot kinds, labels, the empty layout, orphans
// ============================================================================

TEST(HostedPluginCardTest, EachSlotKindRendersTheRightWidget) {
    Rig rig;
    rig.addPlugin({knobSpec("k1", "Knob One"), toggleSpec("t1", "Toggle One"),
                   choiceSpec("c1", "Choice One", {"Sine", "Saw", "Square"})});
    auto card = rig.makeCard();

    auto* knob = childWithId<juce::Slider>(*card, "hostedKnob:k1");
    auto* toggle = childWithId<juce::ToggleButton>(*card, "hostedToggle:t1");
    auto* choice = childWithId<juce::ComboBox>(*card, "hostedChoice:c1");
    ASSERT_NE(knob, nullptr) << "a plain parameter is a knob";
    ASSERT_NE(toggle, nullptr) << "a boolean parameter is a toggle";
    ASSERT_NE(choice, nullptr) << "a discrete parameter with value strings is a choice combo";

    EXPECT_EQ(knob->getSliderStyle(), juce::Slider::RotaryHorizontalVerticalDrag);
    ASSERT_EQ(choice->getNumItems(), 3);
    EXPECT_EQ(choice->getItemText(0), "Sine");
    EXPECT_EQ(choice->getItemText(1), "Saw");
    EXPECT_EQ(choice->getItemText(2), "Square");

    EXPECT_NE(labelWithText(*card, "Knob One"), nullptr) << "a slot with no label override shows the parameter's name";
    EXPECT_NE(labelWithText(*card, "Choice One"), nullptr);
    EXPECT_EQ(toggle->getButtonText(), "Toggle One");

    // All three live in the generic member arrays' places, so the knob grid lays them out and they are on the card.
    EXPECT_TRUE(card->getLocalBounds().contains(knob->getBounds()));
    EXPECT_TRUE(card->getLocalBounds().contains(choice->getBounds()));
    EXPECT_TRUE(card->getLocalBounds().contains(toggle->getBounds()));
}

TEST(HostedPluginCardTest, AnExplicitKindWinsOverTheDerivedOneAndALabelOverrideIsShown) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    rig.module->setCardLayoutOverride(layoutOf({slotFor("a", 0, synth::CardSlotKind::Toggle),
                                                slotFor("b", 1, synth::CardSlotKind::Knob, juce::String("Cutoff")),
                                                // A Choice slot on a parameter with no value strings has nothing to
                                                // offer, so it degrades to a knob rather than an empty combo.
                                                slotFor("c", 2, synth::CardSlotKind::Choice)})
                                          .toVar());
    auto card = rig.makeCard();

    EXPECT_NE(childWithId<juce::ToggleButton>(*card, "hostedToggle:a"), nullptr);
    EXPECT_NE(childWithId<juce::Slider>(*card, "hostedKnob:b"), nullptr);
    EXPECT_NE(labelWithText(*card, "Cutoff"), nullptr) << "the slot's own label replaces the parameter name";
    EXPECT_EQ(labelWithText(*card, "Beta"), nullptr);
    EXPECT_NE(childWithId<juce::Slider>(*card, "hostedKnob:c"), nullptr);
    EXPECT_EQ(childWithId<juce::ComboBox>(*card, "hostedChoice:c"), nullptr);
}

TEST(HostedPluginCardTest, AnEmptyLayoutIsJustTheTwoButtons) {
    Rig rig;
    StubParamTraits notAutomatable;
    notAutomatable.automatable = false;
    rig.addPlugin({StubParamSpec{"hidden", "Hidden", 0.0f, notAutomatable, false}});
    auto card = rig.makeCard();

    EXPECT_TRUE(childrenOfType<juce::Slider>(*card).empty());
    EXPECT_TRUE(childrenOfType<juce::ComboBox>(*card).empty());
    EXPECT_TRUE(childrenOfType<juce::ToggleButton>(*card).empty());

    auto* openEditor = childWithId<juce::TextButton>(*card, "openPluginEditor");
    auto* chooseKnobs = childWithId<juce::TextButton>(*card, "chooseKnobs");
    ASSERT_NE(openEditor, nullptr);
    ASSERT_NE(chooseKnobs, nullptr);
    EXPECT_EQ(chooseKnobs->getButtonText(), "Choose knobs...");
    EXPECT_EQ(openEditor->getY(), chooseKnobs->getY()) << "both buttons share one row";
    EXPECT_LT(openEditor->getRight(), chooseKnobs->getX()) << "Open Editor on the left, Choose knobs... on the right";
    EXPECT_TRUE(openEditor->isVisible());
    EXPECT_TRUE(chooseKnobs->isVisible());

    int requested = 0;
    card->onChooseKnobsRequested = [&requested] { ++requested; };
    chooseKnobs->onClick();
    EXPECT_EQ(requested, 1);
}

TEST(HostedPluginCardTest, AnOrphanSlotRendersNoWidget) {
    Rig rig;
    rig.addPlugin({knobSpec("real", "Real"), knobSpec("other", "Other")});
    // "gone" resolves to nothing on this instance (an id-bearing plugin never rescues by index).
    rig.module->setCardLayoutOverride(layoutOf({slotFor("gone", 7), slotFor("real", 0)}).toVar());
    auto card = rig.makeCard();

    EXPECT_EQ(childrenOfType<juce::Slider>(*card).size(), 1u) << "only the slot that still resolves is drawn";
    EXPECT_NE(childWithId<juce::Slider>(*card, "hostedKnob:real"), nullptr);
    EXPECT_EQ(childWithId<juce::Slider>(*card, "hostedKnob:gone"), nullptr) << "an orphan is hidden, not greyed";
}

TEST(HostedPluginCardTest, ACardBuiltBeforeTheInstanceLoadsFillsInWhenItIsPublished) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma"), knobSpec("d", "Delta")},
                  /*waitForInstance*/ false);
    auto card = rig.makeCard();
    ASSERT_FALSE(rig.module->hasInstance());
    EXPECT_TRUE(childrenOfType<juce::Slider>(*card).empty()) << "no instance yet: a slot is unresolved, not drawn";
    const int heightWithoutKnobs = card->getHeight();

    ASSERT_TRUE(pumpUntil([&] { return rig.module->hasInstance(); }));

    EXPECT_EQ(childrenOfType<juce::Slider>(*card).size(), 4u) << "the card rebuilds when the instance goes live";
    EXPECT_GT(card->getHeight(), heightWithoutKnobs) << "and grows to fit them, like any many-parameter module";
}

// ============================================================================
// 2. Where the layout comes from, and what rebuilds the card
// ============================================================================

TEST(HostedPluginCardTest, TheAutomaticDefaultShowsTheFirstEightAutomatableParametersAsKnobs) {
    Rig rig;
    std::vector<StubParamSpec> specs;
    for (int i = 0; i < 10; ++i)
        specs.push_back(knobSpec("p" + juce::String(i), "Param " + juce::String(i)));
    specs[2].isBypass = true; // the instance's bypass parameter is skipped
    StubParamTraits notAutomatable;
    notAutomatable.automatable = false;
    specs.insert(specs.begin(), StubParamSpec{"first", "Not automatable", 0.0f, notAutomatable, false});
    rig.addPlugin(specs);
    auto card = rig.makeCard();

    const auto sliders = childrenOfType<juce::Slider>(*card);
    ASSERT_EQ(sliders.size(), 8u);
    const char* expected[] = {"p0", "p1", "p3", "p4", "p5", "p6", "p7", "p8"};
    for (size_t i = 0; i < sliders.size(); ++i)
        EXPECT_EQ(sliders[i]->getComponentID(), juce::String("hostedKnob:") + expected[i]) << "slot " << i;
}

TEST(HostedPluginCardTest, ChangingThePerInstanceOverrideRebuildsTheCardBody) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    auto card = rig.makeCard();
    ASSERT_EQ(childrenOfType<juce::Slider>(*card).size(), 3u);
    const int heightBefore = card->getHeight();

    rig.module->setCardLayoutOverride(
        layoutOf({slotFor("c", 2), slotFor("a", 0, synth::CardSlotKind::Auto, juce::String("Renamed"))}).toVar());

    auto sliders = childrenOfType<juce::Slider>(*card);
    ASSERT_EQ(sliders.size(), 2u);
    EXPECT_EQ(sliders[0]->getComponentID(), "hostedKnob:c") << "slot order is the layout's order";
    EXPECT_EQ(sliders[1]->getComponentID(), "hostedKnob:a");
    EXPECT_NE(labelWithText(*card, "Renamed"), nullptr);
    EXPECT_LE(card->getHeight(), heightBefore);

    rig.module->setCardLayoutOverride({}); // clearing it falls back to the automatic default
    EXPECT_EQ(childrenOfType<juce::Slider>(*card).size(), 3u);
}

TEST(HostedPluginCardTest, ChangingThePluginsStoredDefaultRebuildsTheCardBody) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta"), knobSpec("c", "Gamma")});
    auto card = rig.makeCard();
    auto sliders = childrenOfType<juce::Slider>(*card);
    ASSERT_EQ(sliders.size(), 3u);
    for (auto* slider : sliders)
        slider->getProperties().set("marker", 1); // a rebuilt slider is a new object without it

    // A default for some OTHER plugin is not this card's business.
    synth::PluginIdentity other = rig.module->getIdentity();
    other.uid += 1;
    ASSERT_TRUE(rig.store.setDefault(other, layoutOf({slotFor("a", 0)})));
    for (auto* slider : childrenOfType<juce::Slider>(*card))
        EXPECT_TRUE(slider->getProperties().contains("marker")) << "an unrelated plugin's default must not rebuild";

    ASSERT_TRUE(rig.store.setDefault(rig.module->getIdentity(), layoutOf({slotFor("b", 1)})));
    sliders = childrenOfType<juce::Slider>(*card);
    ASSERT_EQ(sliders.size(), 1u);
    EXPECT_EQ(sliders[0]->getComponentID(), "hostedKnob:b");

    ASSERT_TRUE(rig.store.clearDefault(rig.module->getIdentity()));
    EXPECT_EQ(childrenOfType<juce::Slider>(*card).size(), 3u) << "no default left: back to the automatic set";
}

// ============================================================================
// 3. HostedParameterAttachment
// ============================================================================

TEST(HostedParameterAttachmentTest, SliderRoundTripsValueAndText) {
    StubPluginInstance instance(2, 2, "P", 1, "VST3", {knobSpec("k", "Knob", 0.5f)});
    auto* param = instance.getParameters()[0];
    juce::Slider slider;
    slider.setBounds(0, 0, 60, 80);
    HostedParameterAttachment attachment(*param, slider);

    EXPECT_NEAR(slider.getValue(), 0.5, 1e-6) << "the widget starts at the parameter's value";
    EXPECT_NEAR(slider.getMinimum(), 0.0, 1e-9);
    EXPECT_NEAR(slider.getMaximum(), 1.0, 1e-9);

    // parameter -> widget, after the async hop
    param->setValueNotifyingHost(0.25f);
    pump();
    EXPECT_NEAR(slider.getValue(), 0.25, 1e-6);
    EXPECT_EQ(slider.getTextFromValue(0.25), param->getText(0.25f, 1024)) << "the readout is the parameter's own text";

    // typed text -> the parameter's own text parser -> widget value -> parameter
    const double typed = slider.getValueFromText("0.75");
    EXPECT_NEAR(typed, static_cast<double>(param->getValueForText("0.75")), 1e-6);
    slider.setValue(typed, juce::sendNotificationSync);
    EXPECT_NEAR(param->getValue(), 0.75f, 1e-6f);

    // double-click resets to the parameter's own default
    EXPECT_TRUE(slider.isDoubleClickReturnEnabled());
    EXPECT_NEAR(slider.getDoubleClickReturnValue(), static_cast<double>(param->getDefaultValue()), 1e-9);
}

TEST(HostedParameterAttachmentTest, ToggleRoundTripsBothWays) {
    StubPluginInstance instance(2, 2, "P", 1, "VST3", {toggleSpec("t", "Toggle")});
    auto* param = instance.getParameters()[0];
    juce::ToggleButton toggle;
    HostedParameterAttachment attachment(*param, toggle);
    EXPECT_FALSE(toggle.getToggleState());

    param->setValueNotifyingHost(1.0f);
    pump();
    EXPECT_TRUE(toggle.getToggleState());

    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_FLOAT_EQ(param->getValue(), 0.0f);
    toggle.setToggleState(true, juce::sendNotificationSync);
    EXPECT_FLOAT_EQ(param->getValue(), 1.0f);
}

TEST(HostedParameterAttachmentTest, ComboMapsEveryIndexToTheNormalisedValueAndBack) {
    // 3 and 4 entries are the interesting sizes: 1/2 is exact, 1/3 and 2/3 are not.
    for (const auto& strings : {juce::StringArray{"A", "B", "C"}, juce::StringArray{"W", "X", "Y", "Z"}}) {
        StubPluginInstance instance(2, 2, "P", 1, "VST3", {choiceSpec("c", "Choice", strings)});
        auto* param = instance.getParameters()[0];
        juce::ComboBox combo;
        HostedParameterAttachment attachment(*param, combo);

        ASSERT_EQ(combo.getNumItems(), strings.size());
        const int last = strings.size() - 1;
        for (int index = 0; index <= last; ++index) {
            const float normalised = static_cast<float>(index) / static_cast<float>(last);

            // widget -> parameter
            combo.setSelectedItemIndex(index, juce::sendNotificationSync);
            EXPECT_NEAR(param->getValue(), normalised, 1e-6f) << strings.size() << " entries, index " << index;

            // parameter -> widget (from a different index first, so a change is really required)
            combo.setSelectedItemIndex((index + 1) % strings.size(), juce::dontSendNotification);
            param->setValueNotifyingHost(normalised);
            pump();
            EXPECT_EQ(combo.getSelectedItemIndex(), index) << strings.size() << " entries, index " << index;
        }
        // The text the parameter reports for a step is the item shown for it.
        for (int index = 0; index <= last; ++index)
            EXPECT_EQ(combo.getItemText(index),
                      param->getText(static_cast<float>(index) / static_cast<float>(last), 1024));
    }
}

TEST(HostedParameterAttachmentTest, APluginSideChangeReachesTheWidgetOnlyOnTheMessageThread) {
    StubPluginInstance instance(2, 2, "P", 1, "VST3", {knobSpec("k", "Knob")});
    auto* param = instance.getParameters()[0];
    juce::Slider slider;
    HostedParameterAttachment attachment(*param, slider);
    ASSERT_NEAR(slider.getValue(), 0.0, 1e-9);

    // From another thread (audio-thread automation, a plugin's own thread): the callback may only store
    // and hop, so the widget must not have moved when the thread returns.
    std::thread worker([param] { param->setValueNotifyingHost(0.6f); });
    worker.join();
    EXPECT_NEAR(slider.getValue(), 0.0, 1e-9) << "the widget must never be touched from the reporting thread";
    pump();
    EXPECT_NEAR(slider.getValue(), 0.6, 1e-6);

    // A gesture reported from another thread never reaches the gesture callback (which acts on the card).
    int gestures = 0;
    attachment.onGestureChanged = [&gestures](bool) { ++gestures; };
    std::thread gestureWorker([param] {
        param->beginChangeGesture();
        param->endChangeGesture();
    });
    gestureWorker.join();
    EXPECT_EQ(gestures, 0);

    // From the message thread too: still applied by the async update, never inline in the caller's own write.
    param->setValueNotifyingHost(0.4f);
    EXPECT_NEAR(slider.getValue(), 0.6, 1e-6);
    pump();
    EXPECT_NEAR(slider.getValue(), 0.4, 1e-6);
}

TEST(HostedParameterAttachmentTest, AWidgetDrivenWriteIsNotEchoedBackIntoTheParameter) {
    StubPluginInstance instance(2, 2, "P", 1, "VST3", {knobSpec("k", "Knob")});
    auto* param = static_cast<synth::test::StubHostedParameter*>(instance.getParameters()[0]);
    juce::Slider slider;
    HostedParameterAttachment attachment(*param, slider);

    slider.setValue(0.3, juce::sendNotificationSync);
    pump();
    pump();
    EXPECT_EQ(param->setValueCalls.load(), 1) << "one widget change is exactly one parameter write";
    EXPECT_NEAR(slider.getValue(), 0.3, 1e-6);

    // An incoming value does not write back either.
    param->setValueNotifyingHost(0.8f);
    pump();
    EXPECT_EQ(param->setValueCalls.load(), 2);
    EXPECT_NEAR(slider.getValue(), 0.8, 1e-6);
}

TEST(HostedParameterAttachmentTest, ADragIsOneGestureAndAPlainChangeWrapsItsOwnPair) {
    StubPluginInstance instance(2, 2, "P", 1, "VST3", {knobSpec("k", "Knob", 0.5f)});
    auto* param = instance.getParameters()[0];
    GestureProbe probe;
    param->addListener(&probe);
    juce::Slider slider;
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); // the style a card knob has
    slider.setBounds(0, 0, 60, 80);
    HostedParameterAttachment attachment(*param, slider);

    dragSlider(slider, 30.0f);
    EXPECT_EQ(probe.begins, 1) << "the whole drag is one gesture";
    EXPECT_EQ(probe.ends, 1);
    EXPECT_GT(param->getValue(), 0.5f) << "dragging up raises a rotary";

    slider.setValue(0.1, juce::sendNotificationSync); // typed / wheel / programmatic: not a drag
    EXPECT_EQ(probe.begins, 2);
    EXPECT_EQ(probe.ends, 2);
    EXPECT_NEAR(param->getValue(), 0.1f, 1e-6f);
    param->removeListener(&probe);
}

TEST(HostedParameterAttachmentTest, DetachRemovesTheListenerAndStopsAllTraffic) {
    StubPluginInstance instance(2, 2, "P", 1, "VST3", {knobSpec("k", "Knob")});
    auto* param = instance.getParameters()[0];
    juce::Slider slider;
    HostedParameterAttachment attachment(*param, slider);
    int gestures = 0;
    attachment.onGestureChanged = [&gestures](bool) { ++gestures; };

    param->beginChangeGesture();
    param->endChangeGesture();
    ASSERT_EQ(gestures, 2) << "while bound, every gesture on the parameter is reported";

    param->setValueNotifyingHost(0.9f); // queues an async update...
    attachment.detach();                // ...which detach() drops
    EXPECT_FALSE(attachment.isBound());
    pump();
    EXPECT_NEAR(slider.getValue(), 0.0, 1e-9) << "a queued update must not outlive detach()";

    param->beginChangeGesture(); // a listener still registered would be called here
    param->endChangeGesture();
    param->setValueNotifyingHost(0.2f);
    pump();
    EXPECT_EQ(gestures, 2) << "after detach() the attachment hears nothing";
    EXPECT_NEAR(slider.getValue(), 0.0, 1e-9);

    attachment.detach(); // idempotent
}

// ============================================================================
// 4. Undo: one gesture pair = one undo step
// ============================================================================

TEST(HostedPluginCardTest, OneDragOnAHostedKnobIsOneUndoStepAndThePluginSeesOneGesture) {
    Rig rig;
    rig.addPlugin({knobSpec("k", "Knob", 0.5f)}, true, /*trackState*/ true);
    AppUndoManager undo;
    auto card = rig.makeCard(&undo);
    auto* slider = childWithId<juce::Slider>(*card, "hostedKnob:k");
    ASSERT_NE(slider, nullptr);
    ASSERT_GT(slider->getHeight(), 0);

    GestureProbe probe;
    rig.param(0)->addListener(&probe);
    const int serialBefore = undo.getEditSerial();

    dragSlider(*slider, 30.0f);

    EXPECT_EQ(probe.begins, 1) << "the plugin sees exactly one begin";
    EXPECT_EQ(probe.ends, 1) << "and one end";
    EXPECT_GT(rig.param(0)->getValue(), 0.5f);
    EXPECT_EQ(undo.getEditSerial(), serialBefore + 1) << "one whole drag must cost exactly one undo step";
    EXPECT_TRUE(undo.canUndo());
    rig.param(0)->removeListener(&probe);
}

TEST(HostedPluginCardTest, ToggleComboAndPluginEditorGesturesEachCostOneUndoStep) {
    Rig rig;
    rig.addPlugin({toggleSpec("t", "Toggle"), choiceSpec("c", "Choice", {"A", "B", "C"}), knobSpec("k", "Knob")}, true,
                  /*trackState*/ true);
    AppUndoManager undo;
    auto card = rig.makeCard(&undo);
    auto* toggle = childWithId<juce::ToggleButton>(*card, "hostedToggle:t");
    auto* combo = childWithId<juce::ComboBox>(*card, "hostedChoice:c");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(combo, nullptr);

    int serial = undo.getEditSerial();
    toggle->setToggleState(true, juce::sendNotificationSync);
    EXPECT_EQ(undo.getEditSerial(), ++serial);

    combo->setSelectedItemIndex(2, juce::sendNotificationSync);
    EXPECT_EQ(undo.getEditSerial(), ++serial);

    // The plugin's own editor drives the parameter with the same gesture calls; the card follows them.
    auto* knobParam = rig.param(2);
    knobParam->beginChangeGesture();
    knobParam->setValueNotifyingHost(0.7f);
    knobParam->setValueNotifyingHost(0.8f);
    knobParam->endChangeGesture();
    EXPECT_EQ(undo.getEditSerial(), ++serial) << "one begin/end pair is one step however many values it carried";
}

TEST(HostedPluginCardTest, AutomationReflectsIntoTheWidgetWithoutWritingTheParameter) {
    Rig rig;
    rig.addPlugin({knobSpec("k", "Knob")});
    auto card = rig.makeCard();
    auto* slider = childWithId<juce::Slider>(*card, "hostedKnob:k");
    ASSERT_NE(slider, nullptr);
    auto* param = static_cast<synth::test::StubHostedParameter*>(rig.param(0));
    const int writesBefore = param->setValueCalls.load();

    // The automation applier stores with a plain setValue (no listener hears it) and feeds the UI instead.
    param->setValue(0.9f);
    card->reflectParameterValue(param, 0.9f);

    EXPECT_NEAR(slider->getValue(), 0.9, 1e-6);
    pump();
    EXPECT_EQ(param->setValueCalls.load(), writesBefore + 1) << "reflecting must not write back";
}

// ============================================================================
// 5. Lifetime: the instance goes away
// ============================================================================

namespace {
struct EdgeRecorder : HostedPluginModule::InstanceObserver {
    void hostedInstanceGone() override {
        ++gone;
        activeWhenGone = module->getActiveInstanceForEditor();
        destroyedWhenGone = StubPluginInstance::destructionCount().load();
    }
    void hostedInstanceLive() override { ++live; }

    HostedPluginModule* module = nullptr;
    int gone = 0;
    int live = 0;
    juce::AudioPluginInstance* activeWhenGone = nullptr;
    int destroyedWhenGone = -1;
};
} // namespace

TEST(HostedPluginCardTest, TheGoneEdgeFiresBeforeTheInstanceCanBeFreedOnEveryPath) {
    StubBackend backend;
    StubPluginInstance::clearDestructionRecord();
    EdgeRecorder recorder;
    {
        auto module = std::make_unique<HostedPluginModule>(); // never prepared: a retired instance is freed at once
        recorder.module = module.get();
        module->addInstanceObserver(&recorder);

        module->loadPlugin(stubDescription(), backend);
        ASSERT_TRUE(pumpUntil([&] { return module->hasInstance(); }));
        EXPECT_EQ(recorder.live, 1);
        EXPECT_EQ(recorder.gone, 1) << "publishing retires the (empty) previous instance first: one gone, one live";

        // unload: gone fires while the instance still exists, then it is freed.
        const int goneBefore = recorder.gone;
        module->unloadPlugin();
        EXPECT_EQ(recorder.gone, goneBefore + 1);
        EXPECT_EQ(recorder.activeWhenGone, nullptr) << "hasInstance() is already false at the edge";
        EXPECT_EQ(recorder.destroyedWhenGone, 0) << "...but nothing has been destroyed yet";
        EXPECT_EQ(StubPluginInstance::destructionCount().load(), 1) << "and it IS freed right after";

        // destructor: same guarantee.
        module->loadPlugin(stubDescription(), backend);
        ASSERT_TRUE(pumpUntil([&] { return module->hasInstance(); }));
        const int destroyedBefore = StubPluginInstance::destructionCount().load();
        const int goneBeforeDtor = recorder.gone;
        module.reset();
        EXPECT_EQ(recorder.gone, goneBeforeDtor + 1) << "the destructor announces the edge too";
        EXPECT_EQ(recorder.destroyedWhenGone, destroyedBefore) << "before it frees the instance";
        EXPECT_EQ(StubPluginInstance::destructionCount().load(), destroyedBefore + 1);
    }
}

TEST(HostedPluginCardTest, UnloadingThePluginUnbindsTheCardAndEmptiesItsBody) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha"), toggleSpec("t", "Toggle")});
    auto card = rig.makeCard();
    auto* param = rig.param(0);
    ASSERT_EQ(childrenOfType<juce::Slider>(*card).size(), 1u);
    const int heightWithKnobs = card->getHeight();

    rig.module->unloadPlugin();

    EXPECT_TRUE(childrenOfType<juce::Slider>(*card).empty());
    EXPECT_TRUE(childrenOfType<juce::ToggleButton>(*card).empty());
    EXPECT_NE(childWithId<juce::TextButton>(*card, "openPluginEditor"), nullptr) << "the chrome stays";
    EXPECT_NE(childWithId<juce::TextButton>(*card, "chooseKnobs"), nullptr);
    pump(); // the re-measure is deferred one loop turn
    EXPECT_LT(card->getHeight(), heightWithKnobs) << "the card shrinks back";
    (void)param; // the retired parameter may be freed by now: it must not be touched again
}

TEST(HostedPluginCardTest, ReplacingThePluginRebindsTheCardToTheNewInstance) {
    Rig rig;
    rig.addPlugin({knobSpec("k", "Knob")});
    auto card = rig.makeCard();
    auto* oldParam = rig.param(0);
    ASSERT_NE(childWithId<juce::Slider>(*card, "hostedKnob:k"), nullptr);

    // Loading again retires the old instance, publishes a new one, and the card rebuilds around it.
    rig.module->loadPlugin(stubDescription(), rig.backend);
    ASSERT_TRUE(pumpUntil([&] {
        return rig.module->hasInstance() && rig.module->getActiveInstanceForEditor() != nullptr &&
               rig.param(0) != oldParam;
    }));
    pump();

    auto* slider = childWithId<juce::Slider>(*card, "hostedKnob:k");
    ASSERT_NE(slider, nullptr);
    rig.param(0)->setValueNotifyingHost(0.65f);
    pump();
    EXPECT_NEAR(slider->getValue(), 0.65, 1e-6) << "the knob follows the NEW instance's parameter";
}

TEST(HostedPluginCardTest, DeletingTheNodeThroughTheEditorUnbindsBeforeTheInstanceIsFreed) {
    Rig rig;
    StubPluginInstance::clearDestructionRecord();
    rig.addPlugin({knobSpec("a", "Alpha"), knobSpec("b", "Beta")});
    rig.editor.updateComponents();
    ASSERT_EQ(rig.editor.getModuleComponents().size(), 1);
    auto* card = rig.editor.getModuleComponents()[0];
    ASSERT_EQ(card->getNodeId(), rig.nodeId);
    ASSERT_EQ(childrenOfType<juce::Slider>(*card).size(), 2u);

    // requestDeleteModule frees the processor (and the instance) BEFORE the card is torn down, and does not
    // call the card's detachFromProcessor first: the module's own gone edge is what unbinds it.
    rig.editor.requestDeleteModule(rig.nodeId);

    EXPECT_EQ(rig.editor.getModuleComponents().size(), 0);
    EXPECT_EQ(StubPluginInstance::destructionCount().load(), 1) << "the instance is really gone";
    pump(); // a deferred re-measure must find the card (and module) gone and do nothing
}

TEST(HostedPluginCardTest, ClearingTheGraphAfterDetachAllIsSafeWithLiveCards) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha")});
    rig.editor.updateComponents();
    ASSERT_EQ(rig.editor.getModuleComponents().size(), 1);

    // The graph-replacing seam (undo restore, Load, New Patch): detach every card, THEN free the nodes.
    rig.editor.detachAllModuleComponents();
    rig.engine.getGraph().clear();
    rig.editor.updateComponents();
    EXPECT_EQ(rig.editor.getModuleComponents().size(), 0);
    pump();
}

TEST(HostedPluginCardTest, DetachingACardWhoseNodeWasAlreadyFreedTouchesNothing) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha"), choiceSpec("c", "Choice", {"A", "B"})});
    auto card = rig.makeCard();
    ASSERT_EQ(childrenOfType<juce::Slider>(*card).size(), 1u);

    // Free the node (processor, instance, parameters) out from under the card, as an unguarded removal would.
    rig.engine.getGraph().removeNode(rig.nodeId);
    rig.module = nullptr;

    card->detachFromProcessor(); // must not dereference the freed module or any freed parameter
    card.reset();
    pump();
}

TEST(HostedPluginCardTest, ACardDetachedWhileTheModuleLivesLeavesNoListenersOrHooksBehind) {
    Rig rig;
    rig.addPlugin({knobSpec("a", "Alpha")});
    auto card = rig.makeCard();
    auto* param = static_cast<synth::test::StubHostedParameter*>(rig.param(0));
    ASSERT_NE(childWithId<juce::Slider>(*card, "hostedKnob:a"), nullptr);

    card->detachFromProcessor();

    // Nothing is listening any more: none of these may reach a destroyed widget (ASAN would flag it) ...
    param->setValueNotifyingHost(0.5f);
    param->beginChangeGesture();
    param->endChangeGesture();
    rig.module->setCardLayoutOverride(layoutOf({slotFor("a", 0)}).toVar());
    ASSERT_TRUE(rig.store.setDefault(rig.module->getIdentity(), layoutOf({slotFor("a", 0)})));
    rig.module->unloadPlugin();
    pump();
    card.reset();
}

// ============================================================================
// 6. PNG render hook
// ============================================================================

// Renders the card headlessly so a human can look at it without driving the real app: the image-content
// assertion always runs, the PNG is only written when PLUGIN_CARD_PNG names a path.
TEST(HostedPluginCardTest, CardRendersToPngForVisualInspection) {
    Rig rig;
    std::vector<StubParamSpec> specs;
    const char* names[] = {"Cutoff", "Resonance", "Drive", "Attack", "Release", "Mix"};
    for (int i = 0; i < 6; ++i)
        specs.push_back(knobSpec("k" + juce::String(i), names[i], 0.15f * static_cast<float>(i + 1)));
    specs.push_back(toggleSpec("t0", "Unison"));
    specs.push_back(choiceSpec("c0", "Waveform", {"Sine", "Saw", "Square", "Triangle"}));
    rig.addPlugin(specs);
    auto card = rig.makeCard();

    synth::theme::AppLookAndFeel lf;
    card->setLookAndFeel(&lf);
    ASSERT_GT(card->getWidth(), 0);
    ASSERT_GT(card->getHeight(), 0);

    // SoftwareImageType(): on Windows the default (native) image type is Direct2D-backed, and
    // painting into it then reading pixels back on a GPU-less CI runner yields an all-zero image
    // (FRO242). Force a software-backed bitmap so getPixelAt() reads what paint() actually drew.
    juce::Image image(juce::Image::ARGB, card->getWidth(), card->getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    EXPECT_NO_THROW(card->paintEntireComponent(g, true));

    bool hasOpaquePixel = false;
    for (int y = 0; y < image.getHeight() && !hasOpaquePixel; ++y)
        for (int x = 0; x < image.getWidth() && !hasOpaquePixel; ++x)
            hasOpaquePixel = image.getPixelAt(x, y).getAlpha() > 0;
    EXPECT_TRUE(hasOpaquePixel);

    if (const char* pngPath = std::getenv("PLUGIN_CARD_PNG")) {
        juce::File out(pngPath);
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream(out);
        ASSERT_TRUE(stream.openedOk());
        juce::PNGImageFormat png;
        EXPECT_TRUE(png.writeImageToStream(image, stream));
        std::cout << "PLUGIN_CARD_PNG written: " << out.getFullPathName() << " (" << card->getWidth() << "x"
                  << card->getHeight() << ")" << std::endl;
    }
    card->setLookAndFeel(nullptr);
}
