// ColourPickerEscapeTests.cpp -- Escape in the colour picker, from every control that can hold focus: one fade (the
// box's alpha falls and never rises, no leaving picture on top of the live window), the colour goes back to the one
// the picker opened with, and focus returns to the button that opened it. The picker opens in a real PopoverBox on a
// showing window and a key travels the way JUCE sends it: the focused component, then each parent up to the box. Also
// the opening frames of the picker and the second click on its swatch.
#include "../../TestWait.h"
#include "../Layout/NativeWindowTestGuard.h"
#include "../Layout/PopupAlphaTrace.h"
#include "../Layout/PopupTestCleanup.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Layout/PopupMotion.h"
#include <functional>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace synth::ui;
using synth::test::isNonDecreasing;
using synth::test::isNonIncreasing;

namespace {

struct Seen {
    std::vector<juce::Colour> previews;
    std::vector<juce::Colour> commits;
};

void pumpUntil(const std::function<bool()>& done, int ms = 3000) {
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32)ms;
    while (!done() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
}

void drain() {
    for (int pass = 0; pass < 3; ++pass) {
        bool reached = false;
        juce::MessageManager::callAsync([&reached] { reached = true; });
        while (!reached)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
    }
}

int callOutBoxes() {
    int count = 0;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)) != nullptr)
            ++count;
    return count;
}

void collectSliders(juce::Component& root, std::vector<juce::Slider*>& out) {
    for (auto* child : root.getChildren()) {
        if (auto* slider = dynamic_cast<juce::Slider*>(child))
            out.push_back(slider);
        else
            collectSliders(*child, out);
    }
}

// The button that opens the picker; counts the times it is given keyboard focus.
class Opener : public juce::TextButton {
public:
    Opener()
        : juce::TextButton("Colour") {}
    void focusGained(FocusChangeType) override { ++focusGains; }
    int focusGains = 0;
};

// What a real Escape does: the focused component gets it, then its parents, up to the box.
// With real focus on `start` the key goes through the window's own key path (key listeners first); without it, up the
// parents.
bool sendEscape(juce::Component& start) {
    const juce::KeyPress escape(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0);
    if (juce::Component::getCurrentlyFocusedComponent() == &start && start.getPeer() != nullptr)
        return start.getPeer()->handleKeyPress(escape);
    for (auto* c = &start; c != nullptr; c = c->getParentComponent())
        if (c->keyPressed(escape))
            return true;
    return false;
}

struct Target {
    std::string name;
    std::function<juce::Component*(ColourPickerPopup&)> focus; // gives the component that takes the key
};

std::vector<Target> targets() {
    std::vector<Target> list;
    list.push_back({"saturation and brightness field",
                    [](ColourPickerPopup& p) { return p.findChildWithID("colourPickerSaturationBrightness"); }});
    list.push_back({"hue bar", [](ColourPickerPopup& p) { return p.findChildWithID("colourPickerHue"); }});
    for (int i = 0; i < 3; ++i) {
        list.push_back({"slider " + std::to_string(i), [i](ColourPickerPopup& p) {
                            std::vector<juce::Slider*> sliders;
                            collectSliders(p, sliders);
                            return static_cast<juce::Component*>(sliders[(size_t)i]);
                        }});
        list.push_back({"slider " + std::to_string(i) + " value box", [i](ColourPickerPopup& p) -> juce::Component* {
                            std::vector<juce::Slider*> sliders;
                            collectSliders(p, sliders);
                            for (auto* part : sliders[(size_t)i]->getChildren())
                                if (auto* label = dynamic_cast<juce::Label*>(part)) {
                                    label->showEditor();
                                    return label->getCurrentTextEditor();
                                }
                            return nullptr;
                        }});
    }
    list.push_back(
        {"add to favourites", [](ColourPickerPopup& p) { return p.findChildWithID("colourPickerAddFavourite"); }});
    list.push_back({"first favourite", [](ColourPickerPopup& p) -> juce::Component* {
                        for (auto* child : p.getChildren())
                            if (child->getName() == "favouriteSwatch")
                                return child;
                        return nullptr;
                    }});
    return list;
}

class ColourPickerEscape : public ::testing::Test {
protected:
    void SetUp() override {
        if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr)
            GTEST_SKIP() << "needs a display";
        if (nativeWindowsAbortHere())
            GTEST_SKIP() << "native windows abort the process under the ASAN job's Xvfb";
        juce::MessageManager::getInstance();
        drain();
        synth::testmode::setAppInFrontForTest(true);
        PopupMotion::setAnimateOffScreenForTest(true);
        app.setBounds(0, 0, 700, 500);
        opener.setBounds(500, 10, 80, 24);
        app.addAndMakeVisible(opener);
        app.addToDesktop(0);
        app.setVisible(true);
    }
    void TearDown() override {
        drain();
        synth::test::finishPopupTest();
    }

    PopoverSlot::Result openPicker(PopoverSlot& slot, const std::shared_ptr<Seen>& seen) {
        return slot.open(
            [seen, this] {
                auto popup = std::make_unique<ColourPickerPopup>(
                    initial, nullptr, [seen](juce::Colour c) { seen->previews.push_back(c); },
                    [seen](juce::Colour c) { seen->commits.push_back(c); });
                return popup;
            },
            area, &app, {.trigger = &opener});
    }

    void stepIn(juce::CallOutBox& box, std::vector<float>& alphas) {
        const double total = popup_motion::durationMs(popup_motion::Phase::In, false);
        for (const double fraction : {0.25, 0.5, 0.75, 1.5}) {
            PopupMotion::stepMotionForTest(box, total * fraction);
            alphas.push_back(box.getAlpha());
        }
    }

    juce::Component app;
    Opener opener;
    const juce::Rectangle<int> area{200, 200, 24, 24};
    const juce::Colour initial = juce::Colour(0xff3366cc);
};

} // namespace

TEST_F(ColourPickerEscape, ItFadesInFromNothingAndRisesToWholeInOneMove) {
    PopoverSlot slot;
    auto seen = std::make_shared<Seen>();
    ASSERT_EQ(openPicker(slot, seen), PopoverSlot::Result::Opened);
    auto* box = slot.box();
    ASSERT_NE(box, nullptr);
    std::vector<float> alphas{box->getAlpha()};
    EXPECT_LT(alphas.front(), 1.0f) << "the first frame is not already whole";
    stepIn(*box, alphas);
    EXPECT_TRUE(isNonDecreasing(alphas));
    EXPECT_EQ(alphas.back(), 1.0f);
    slot.close();
}

TEST_F(ColourPickerEscape, ASecondPressOnTheOpenersAreaClosesItAndIsConsumed) {
    PopoverSlot slot;
    auto seen = std::make_shared<Seen>();
    ASSERT_EQ(openPicker(slot, seen), PopoverSlot::Result::Opened);
    EXPECT_EQ(openPicker(slot, seen), PopoverSlot::Result::ClosedIt) << "the swatch toggles the picker";
    EXPECT_EQ(callOutBoxes(), 1) << "no second picker";
}

TEST_F(ColourPickerEscape, EscapeFromEveryControlFadesOnceRestoresTheColourAndReturnsFocusToTheOpener) {
    // Whether this machine gives keyboard focus to a control of a test window at all.
    opener.grabKeyboardFocus();
    drain();
    const bool focusWorks = opener.focusGains > 0;

    for (const auto& target : targets()) {
        SCOPED_TRACE(target.name);
        opener.focusGains = 0;
        PopoverSlot slot;
        auto seen = std::make_shared<Seen>();
        ASSERT_EQ(openPicker(slot, seen), PopoverSlot::Result::Opened);
        auto* box = slot.box();
        ASSERT_NE(box, nullptr);
        auto* popup = dynamic_cast<ColourPickerPopup*>(slot.content());
        ASSERT_NE(popup, nullptr);
        std::vector<float> alphas{box->getAlpha()};
        stepIn(*box, alphas);

        popup->setCurrentColourForTest(juce::Colours::orange); // something to put back
        auto* focused = target.focus(*popup);
        ASSERT_NE(focused, nullptr);
        focused->grabKeyboardFocus();
        drain();
        auto* real = juce::Component::getCurrentlyFocusedComponent();
        auto* origin = real != nullptr && box->isParentOf(real) ? real : focused;
        std::cout << "[  RECORD  ] " << target.name << ": real focus on it = " << (origin == focused) << "\n";

        sendEscape(*origin);
        drain(); // a text field posts its Escape as a message

        // The drain ran the fade on the real clock for however long a slow machine took; read its start at 0 ms.
        if (PopupMotion::isDismissing(*box))
            PopupMotion::stepMotionForTest(*box, 0.0);
        std::vector<float> fall{box->getAlpha()};
        int maxGhosts = PopupMotion::getNumLeavingGhosts();
        bool liveFadeAndGhost = false;
        EXPECT_TRUE(PopupMotion::isDismissing(*box)) << "Escape did not start the live fade";
        for (const double ms : {30.0, 60.0, 90.0, 120.0, 400.0}) { // the elapsed time since the leave began
            if (!PopupMotion::isDismissing(*box))
                break;
            PopupMotion::stepMotionForTest(*box, ms);
            if (PopupMotion::isDismissing(*box)) {
                fall.push_back(box->getAlpha());
                liveFadeAndGhost = liveFadeAndGhost || PopupMotion::getNumLeavingGhosts() > 0;
            }
            maxGhosts = std::max(maxGhosts, PopupMotion::getNumLeavingGhosts());
        }
        std::string frames;
        for (float f : fall)
            frames += std::to_string(f) + " ";
        std::cout << "[  RECORD  ]   fall frames: " << frames << " ghosts=" << maxGhosts << "\n";
        EXPECT_GT(fall.front(), 0.9f) << "the leave starts whole";
        EXPECT_TRUE(isNonIncreasing(fall)) << "the alpha rose again during the fall: " << frames;
        EXPECT_FALSE(liveFadeAndGhost) << "a leaving picture sat on top of the live fade";
        EXPECT_LE(maxGhosts, 1);

        pumpUntil([&] { return !slot.isOpen() && callOutBoxes() == 0; });
        EXPECT_EQ(callOutBoxes(), 0);
        EXPECT_EQ(seen->previews.empty() ? juce::Colour() : seen->previews.back(), initial)
            << "the live preview went back to the opening colour";
        ASSERT_EQ(seen->commits.size(), 1u);
        EXPECT_EQ(seen->commits.back(), initial) << "and so did the commit";
        if (focusWorks)
            EXPECT_GT(opener.focusGains, 0) << "focus did not return to the button that opened the picker";
        pumpUntil([] { return PopupMotion::getNumLeavingGhosts() == 0; });
        EXPECT_EQ(PopupMotion::getNumLeavingGhosts(), 0);
    }
}
