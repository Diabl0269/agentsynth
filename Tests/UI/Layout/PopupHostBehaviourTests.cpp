// PopupHostBehaviourTests.cpp -- what every popover does once it opens through PopoverSlot (Layout/PopupHost.h),
// whatever its content: it fades in once (instantly under Animations Off), a second request while it is open closes
// it and never opens another, one while it leaves does nothing, Escape (also from a text field inside it) and a press
// on its trigger fade it out once, and a press elsewhere closes it through one leaving fade. Each content class named
// in PopoverRegistry.h is built here (or exempt with its reason) and put through the same paths in the three motion
// modes; the frames of the whole open-and-close sequence must rise once and fall once (isUnimodal), which is what a
// close-and-reopen flicker breaks.
#include "../../TestWait.h"
#include "AudioEngine/AudioEngine.h"
#include "NativeWindowTestGuard.h"
#include "PopoverRegistry.h"
#include "PopupAlphaTrace.h"
#include "PopupTestCleanup.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/DrumMachine/DrumIdeasPanel.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/HostedOutputs/HostedOutputsPanel.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/MidiRemote/ActionPicker/ActionPickerComponent.h"
#include "UI/MidiRemote/AddController/AddControllerPopover.h"
#include "UI/ModuleViews/EQBandTypeMenu.h"
#include "UI/ModuleViews/ShapeEditor/ShapeStackPanel.h"
#include "UI/ModuleViews/StepGrid/StepGridChordPicker.h"
#include "UI/ModuleViews/StepGrid/StepGridRandomizePanel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeFlyout.h"
#include "UI/Timeline/MidiDestinationPicker.h"
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace synth::ui;
using synth::test::isNonDecreasing;
using synth::test::isNonIncreasing;
using synth::test::isUnimodal;
using synth::test::popoverUsers;

namespace {

using Factory = std::function<std::unique_ptr<juce::Component>()>;

struct ContentEntry {
    const char* name;
    Factory make;          // empty when exempt
    const char* exemption; // why it cannot be built headless; null when `make` is set
};

std::unique_ptr<juce::Component> sized(std::unique_ptr<juce::Component> content) {
    if (content->getWidth() <= 0 || content->getHeight() <= 0)
        content->setSize(280, 240);
    return content;
}

// The Outputs panel on a graph with no Hosted Plugin node: it shows its title and footer, which is all the open, close
// and fade frames need. Owns the engine and editor it points at.
struct OutputsPanelRig {
    AudioEngine engine;
    GraphEditor editor{engine};
};
struct OwnedOutputsPanel
    : private OutputsPanelRig
    , public synth::ui::HostedOutputsPanel {
    OwnedOutputsPanel()
        : synth::ui::HostedOutputsPanel(editor, juce::AudioProcessorGraph::NodeID()) {}
};

std::vector<ContentEntry> contentEntries() {
    const auto colourPicker = [] {
        return std::make_unique<ColourPickerPopup>(
            juce::Colours::red, nullptr, [](juce::Colour) {}, [](juce::Colour) {});
    };
    const auto matrixPicker = [] {
        std::vector<ModMatrixPicker::Item> items;
        for (int i = 1; i <= 4; ++i) {
            ModMatrixPicker::Item item;
            item.id = i;
            item.text = "Row " + juce::String(i);
            items.push_back(item);
        }
        return std::make_unique<ModMatrixPicker>("thing", std::move(items), 0, [](int) {});
    };
    const auto keyPicker = [] {
        return std::make_unique<KeyPicker>(KeyPicker::Options{}, [](std::optional<synth::MusicalScale>) {});
    };
    return {
        {"ColourPickerPopup", colourPicker, nullptr},
        {"ModMatrixPicker", matrixPicker, nullptr},
        {"KeyPicker", keyPicker, nullptr},
        {"EQBandTypeMenu", [] { return std::make_unique<synth::ui::EQBandTypeMenu>(synth::eq::BandType::LowCut, 24); },
         nullptr},
        {"DrumIdeasPanel", [] { return std::make_unique<DrumIdeasPanel>([] { return DrumIdeasContext{}; }); }, nullptr},
        {"DrawShapeFlyout",
         [] { return std::make_unique<DrawShapeFlyout>(DrawShape::Free, DrawShapeFlyout::HintFn{}, [](DrawShape) {}); },
         nullptr},
        {"ShapeStackPanel", [] { return std::make_unique<ShapeStackPanel>(ringhelpers::StackOptions{}); }, nullptr},
        {"StepGridChordPicker",
         [] {
             return std::make_unique<StepGridChordPicker>(stepgrid::chordPickerContent(60, synth::MusicalScale{}),
                                                          synth::SequencerChord{});
         },
         nullptr},
        {"StepGridRandomizePanel",
         [] {
             return std::make_unique<StepGridRandomizePanel>(synth::sequencer_ops::RandomizeSettings{},
                                                             synth::MusicalScale{}, 3);
         },
         nullptr},
        {"MidiDestinationPicker",
         [] {
             return std::make_unique<MidiDestinationPicker>([] { return std::vector<MidiDestinationPicker::Option>{}; },
                                                            [](juce::uint32, bool) {});
         },
         nullptr},
        {"ActionPickerComponent", [] { return std::make_unique<ActionPickerComponent>(); }, nullptr},
        {"AddControllerPopover",
         [] {
             return std::make_unique<AddControllerPopover>(std::vector<AddControllerPopover::DeviceRow>{},
                                                           std::vector<synth::midi::TemplateInfo>{});
         },
         nullptr},
        {"juce::ColourSelector",
         [] {
             auto selector = std::make_unique<juce::ColourSelector>(juce::ColourSelector::showColourAtTop |
                                                                    juce::ColourSelector::showSliders);
             selector->setSize(280, 340);
             return selector;
         },
         nullptr},
        {"CardLayoutControlPanel", {}, "exempt: built from a live card's control options inside an editing session"},
        {"CardLayoutOnCardEditor", {}, "exempt: opens a session on a card of a live graph with an undo manager"},
        {"ControlStylePanel", {}, "exempt: needs a GraphEditor and one of its cards"},
        {"HostedOutputsPanel", [] { return std::make_unique<OwnedOutputsPanel>(); }, nullptr},
        {"AccountFlowPanel", {}, "exempt: needs the account service and its sign-in state"},
        {"DualIOPerModulePopupContent", {}, "exempt: the class is private to PreferencesSettingsTabGraphBehaviour.cpp"},
    };
}

// Offers a key to `start` and then each parent in turn, the way JUCE does, until one takes it.
bool keyReachesUp(juce::Component& start, const juce::KeyPress& key) {
    for (auto* c = &start; c != nullptr; c = c->getParentComponent())
        if (c->keyPressed(key))
            return true;
    return false;
}

enum class Mode { Full, ReduceMotion, AnimationsOff };

const char* modeName(Mode mode) {
    switch (mode) {
    case Mode::Full:
        return "Full";
    case Mode::ReduceMotion:
        return "Reduce motion";
    case Mode::AnimationsOff:
        return "Animations off";
    }
    return "";
}

// Puts the motion mode back whatever a test did to it.
struct ModeGuard {
    explicit ModeGuard(Mode mode)
        : before_(animationMode()) {
        setAnimationMode(mode == Mode::AnimationsOff  ? AnimationMode::off
                         : mode == Mode::ReduceMotion ? AnimationMode::reduced
                                                      : AnimationMode::full);
    }
    ~ModeGuard() { setAnimationMode(before_); }
    AnimationMode before_;
};

bool pumpUntil(const std::function<bool()>& done) {
    const auto deadline = juce::Time::getMillisecondCounter() + 3000;
    while (!done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    return done();
}

void drainMessageQueue() {
    for (int pass = 0; pass < 3; ++pass) {
        bool reached = false;
        juce::MessageManager::callAsync([&reached] { reached = true; });
        while (!reached)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
}

int callOutBoxesOnDesktop() {
    int count = 0;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)) != nullptr)
            ++count;
    return count;
}

juce::TextEditor* firstTextEditorIn(juce::Component& root) {
    if (auto* editor = dynamic_cast<juce::TextEditor*>(&root); editor != nullptr && editor->isShowing())
        return editor;
    for (auto* child : root.getChildren())
        if (auto* found = firstTextEditorIn(*child))
            return found;
    return nullptr;
}

juce::KeyPress escapeKey() { return juce::KeyPress(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0); }

class PopupHostBehaviour : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        drainMessageQueue();
        synth::testmode::setAppInFrontForTest(true);
        PopupMotion::setAnimateOffScreenForTest(true);
        app.setBounds(0, 0, 700, 500);
        trigger.setBounds(500, 10, 80, 24);
        app.addAndMakeVisible(trigger);
        app.addToDesktop(0);
        app.setVisible(true);
    }
    void TearDown() override {
        drainMessageQueue();
        synth::test::finishPopupTest();
    }

    // Opens `entry` through a slot and records the arrival's frames; null when it did not open.
    juce::CallOutBox* openAndArrive(PopoverSlot& slot, const ContentEntry& entry, Mode mode, std::vector<float>& alphas,
                                    PopoverOptions options = {}) {
        options.trigger = &trigger;
        const auto result = slot.open([&entry] { return sized(entry.make()); }, area, &app, std::move(options));
        EXPECT_EQ(result, PopoverSlot::Result::Opened);
        auto* box = slot.box();
        EXPECT_NE(box, nullptr);
        if (box == nullptr)
            return nullptr;
        alphas.push_back(box->getAlpha());
        if (mode == Mode::AnimationsOff) {
            EXPECT_EQ(alphas.front(), 1.0f) << "with Animations off it is whole at once";
            return box;
        }
        EXPECT_LT(alphas.front(), 1.0f) << "the first frame is not already whole: it did not pop in";
        const double total = popup_motion::durationMs(popup_motion::Phase::In, mode == Mode::ReduceMotion);
        for (const double fraction : {0.25, 0.5, 0.75, 1.5}) {
            PopupMotion::stepMotionForTest(*box, total * fraction);
            alphas.push_back(box->getAlpha());
        }
        EXPECT_TRUE(isNonDecreasing(alphas)) << "it only gets more opaque on the way in";
        EXPECT_EQ(alphas.back(), 1.0f) << "and lands whole; frames " << [&] {
            std::string t;
            for (float f : alphas)
                t += std::to_string(f) + " ";
            return t;
        }();
        return box;
    }

    // Steps the live window's leaving fade and records its frames.
    std::vector<float> fadeOutFrames(juce::CallOutBox& box, Mode mode) {
        std::vector<float> frames{box.getAlpha()};
        const double total = popup_motion::durationMs(popup_motion::Phase::Out, mode == Mode::ReduceMotion);
        for (const double fraction : {0.25, 0.5, 0.75, 0.99})
            if (PopupMotion::isDismissing(box)) {
                PopupMotion::stepMotionForTest(box, total * fraction);
                frames.push_back(box.getAlpha());
            }
        return frames;
    }

    // After a close the box is gone: no slot, no window, no leaving picture.
    void expectGone(PopoverSlot& slot) {
        EXPECT_TRUE(pumpUntil([&] { return !slot.isOpen() && callOutBoxesOnDesktop() == 0; }))
            << "the popover is still there: open=" << slot.isOpen() << " boxes=" << callOutBoxesOnDesktop()
            << " trigger=" << trigger.getScreenBounds().toString() << " area=" << area.toString();
        EXPECT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; }));
    }

    void expectOneLiveFade(PopoverSlot& slot, juce::CallOutBox& box, Mode mode, std::vector<float>& alphas) {
        if (mode == Mode::AnimationsOff) {
            expectGone(slot);
            return;
        }
        ASSERT_TRUE(PopupMotion::isDismissing(box)) << "it closed at once instead of fading";
        EXPECT_TRUE(box.isVisible());
        EXPECT_TRUE(box.isCurrentlyModal()) << "it stays modal while it fades, so the press that closed it is consumed";
        const auto frames = fadeOutFrames(box, mode);
        EXPECT_GT(frames.front(), 0.5f) << "the leave starts whole";
        EXPECT_TRUE(isNonIncreasing(frames));
        alphas.insert(alphas.end(), frames.begin(), frames.end());
        EXPECT_TRUE(isUnimodal(alphas)) << "the popover flickered between opening and closing";
        expectGone(slot);
    }

    void forEachContentAndMode(const std::function<void(const ContentEntry&, Mode)>& scenario) {
        for (const auto& entry : contentEntries()) {
            if (!entry.make)
                continue;
            for (const Mode mode : {Mode::Full, Mode::ReduceMotion, Mode::AnimationsOff}) {
                SCOPED_TRACE(std::string(entry.name) + " / " + modeName(mode));
                ModeGuard guard(mode);
                scenario(entry, mode);
                if (::testing::Test::HasFatalFailure())
                    return;
            }
        }
    }

    juce::Component app;
    juce::TextButton trigger{"Open"};
    const juce::Rectangle<int> area{200, 200, 24, 24};
};

} // namespace

TEST(PopupHostRegistry, EveryRegisteredContentHasAFactoryOrAnExemption) {
    std::set<std::string> registered;
    for (const auto& user : popoverUsers())
        for (const char* content : user.contents)
            registered.insert(content);
    std::set<std::string> built;
    std::vector<std::string> exemptions;
    for (const auto& entry : contentEntries()) {
        EXPECT_TRUE(built.insert(entry.name).second) << entry.name << " is listed twice";
        EXPECT_EQ(entry.make == nullptr, entry.exemption != nullptr) << entry.name << ": a factory or an exemption";
        if (entry.exemption != nullptr) {
            EXPECT_TRUE(std::string(entry.exemption).rfind("exempt: ", 0) == 0 && std::strlen(entry.exemption) > 14)
                << entry.name << ": give the reason as 'exempt: <reason>'";
            exemptions.push_back(std::string(entry.name) + " (" + entry.exemption + ")");
        }
    }
    for (const auto& name : registered)
        EXPECT_TRUE(built.count(name) > 0) << name << " opens in a popover but has no factory or exemption here";
    for (const auto& name : built)
        EXPECT_TRUE(registered.count(name) > 0) << name << " is no longer opened by any file in PopoverRegistry.h";
    EXPECT_LE(exemptions.size(), 5u) << "exemptions may only shrink";
    for (const auto& exemption : exemptions)
        std::cout << "[  EXEMPT  ] " << exemption << "\n";
}

TEST_F(PopupHostBehaviour, EveryContentFadesInOnceAndLandsWholeInEachMode) {
    forEachContentAndMode([this](const ContentEntry& entry, Mode mode) {
        PopoverSlot slot;
        std::vector<float> alphas;
        auto* box = openAndArrive(slot, entry, mode, alphas);
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(slot.isOpen());
        EXPECT_EQ(callOutBoxesOnDesktop(), 1);
        slot.close();
        expectOneLiveFade(slot, *box, mode, alphas);
    });
}

TEST_F(PopupHostBehaviour, ASecondRequestClosesItAndNeverOpensAnotherAndOneWhileLeavingIsIgnored) {
    forEachContentAndMode([this](const ContentEntry& entry, Mode mode) {
        PopoverSlot slot;
        std::vector<float> alphas;
        auto* box = openAndArrive(slot, entry, mode, alphas);
        ASSERT_NE(box, nullptr);
        const auto again = slot.open([&entry] { return sized(entry.make()); }, area, &app, {.trigger = &trigger});
        EXPECT_EQ(again, PopoverSlot::Result::ClosedIt) << "the opener's button closes the popover it opened";
        EXPECT_LE(callOutBoxesOnDesktop(), 1) << "a second box was opened";
        if (mode != Mode::AnimationsOff) {
            const auto whileLeaving =
                slot.open([&entry] { return sized(entry.make()); }, area, &app, {.trigger = &trigger});
            EXPECT_EQ(whileLeaving, PopoverSlot::Result::Ignored);
            EXPECT_EQ(callOutBoxesOnDesktop(), 1);
        }
        expectOneLiveFade(slot, *box, mode, alphas);

        std::vector<float> second;
        auto* reopened = openAndArrive(slot, entry, mode, second);
        EXPECT_NE(reopened, nullptr) << "after it has gone the button opens it again";
        slot.close();
        if (reopened != nullptr)
            expectOneLiveFade(slot, *reopened, mode, second);
    });
}

TEST_F(PopupHostBehaviour, EscapeFadesItOutOnceFromTheBoxAndFromATextFieldInside) {
    forEachContentAndMode([this](const ContentEntry& entry, Mode mode) {
        for (const bool fromTextField : {false, true}) {
            PopoverSlot slot;
            std::vector<float> alphas;
            auto* box = openAndArrive(slot, entry, mode, alphas);
            ASSERT_NE(box, nullptr);
            juce::TextEditor* editor = fromTextField ? firstTextEditorIn(*slot.content()) : nullptr;
            SCOPED_TRACE(std::string(fromTextField ? "from a text field" : "from the box") +
                         (editor != nullptr ? " (editor found)" : " (no editor)"));
            if (fromTextField && editor == nullptr) {
                slot.close();
                expectOneLiveFade(slot, *box, mode, alphas);
                continue; // this content has no text field
            }
            if (editor != nullptr) {
                EXPECT_TRUE(keyReachesUp(*editor, escapeKey())) << "nothing took Escape from the text field";
            } else
                EXPECT_TRUE(box->keyPressed(escapeKey()));
            if (mode != Mode::AnimationsOff) {
                box->keyPressed(escapeKey()); // a second Escape while it fades does nothing
                EXPECT_TRUE(PopupMotion::isDismissing(*box));
            }
            expectOneLiveFade(slot, *box, mode, alphas);
        }
    });
}

TEST_F(PopupHostBehaviour, APressOnTheTriggerOrItsAreaClosesItThroughOneFadeAndIsConsumed) {
    forEachContentAndMode([this](const ContentEntry& entry, Mode mode) {
        for (const auto where : {trigger.getScreenBounds().getCentre(), area.getCentre()}) {
            PopoverSlot slot;
            std::vector<float> alphas;
            auto* box = openAndArrive(slot, entry, mode, alphas);
            ASSERT_NE(box, nullptr);
            test_hooks::popoverMouseForTest() = where;
            box->inputAttemptWhenModal();
            if (mode != Mode::AnimationsOff)
                box->inputAttemptWhenModal(); // a second press while it fades does nothing
            expectOneLiveFade(slot, *box, mode, alphas);
        }
    });
}

TEST_F(PopupHostBehaviour, APressElsewhereClosesItThroughOneLeavingFade) {
    forEachContentAndMode([this](const ContentEntry& entry, Mode mode) {
        PopoverSlot slot;
        std::vector<float> alphas;
        auto* box = openAndArrive(slot, entry, mode, alphas);
        ASSERT_NE(box, nullptr);
        test_hooks::popoverMouseForTest() = juce::Point<int>(650, 450);
        box->inputAttemptWhenModal();
        if (mode != Mode::AnimationsOff) {
            ASSERT_TRUE(pumpUntil([] { return PopupMotion::getNumLeavingGhosts() > 0; })) << "it just vanished";
            EXPECT_EQ(PopupMotion::getNumLeavingGhosts(), 1) << "one fade, not two";
            std::vector<float> frames;
            for (const double ms : {0.0, 30.0, 60.0, 90.0, 200.0}) {
                PopupMotion::stepLeavingGhostsForTest(ms);
                const auto ghosts = PopupMotion::getLeavingGhostsForTest();
                if (!ghosts.empty())
                    frames.push_back(ghosts.front()->getAlpha());
            }
            ASSERT_GE(frames.size(), 2u);
            EXPECT_GT(frames.front(), 0.5f) << "the picture starts whole";
            EXPECT_TRUE(isNonIncreasing(frames));
            alphas.insert(alphas.end(), frames.begin(), frames.end());
            EXPECT_TRUE(isUnimodal(alphas)) << "the popover flickered between opening and closing";
        }
        expectGone(slot);
    });
}

TEST_F(PopupHostBehaviour, APressWhileTheOpeningHoldIsStillDownIsNotADismissal) {
    ModeGuard guard(Mode::Full);
    bool holdDown = true;
    PopoverSlot slot;
    const auto result = slot.open([] { return sized(std::make_unique<juce::Component>()); }, area, &app,
                                  {.trigger = &trigger, .holdGestureDown = [&holdDown] { return holdDown; }});
    ASSERT_EQ(result, PopoverSlot::Result::Opened);
    auto* box = slot.box();
    ASSERT_NE(box, nullptr);
    test_hooks::popoverMouseForTest() = juce::Point<int>(650, 450);
    box->inputAttemptWhenModal();
    EXPECT_TRUE(box->isVisible()) << "the release that ends the opening press must not close it";
    EXPECT_TRUE(slot.isOpen());
    holdDown = false;
    box->inputAttemptWhenModal();
    expectGone(slot);
}
