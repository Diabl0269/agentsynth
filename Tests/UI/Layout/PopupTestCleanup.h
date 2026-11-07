// PopupTestCleanup.h -- what a test that opens popovers owes the tests after it. A call-out left open, a leaving
// picture still fading, a modal component or a test seam left set changes how every later test in the same process
// sees clicks, keys and animations. A popup fixture's TearDown calls finishPopupTest().
#pragma once

#include "../../TestWait.h"
#include "TestMode/AutomationHiding.h"
#include "UI/Layout/PopupHost.h"
#include "UI/Layout/PopupMotion.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::test {

inline int popupCallOutsOnDesktop() {
    int count = 0;
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)) != nullptr)
            ++count;
    return count;
}

/** Closes every call-out still open (Escape, as a user would), then runs the message loop until no call-out and no
 *  leaving picture is left. */
inline void closeOpenPopups(int timeoutMs = 3000) {
    using synth::ui::PopupMotion;
    const juce::KeyPress escape(juce::KeyPress::escapeKey, juce::ModifierKeys(), 0);
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* box = dynamic_cast<juce::CallOutBox*>(desktop.getComponent(i)); box != nullptr)
            if (!PopupMotion::isDismissing(*box))
                box->keyPressed(escape);
    waitUntil([] { return PopupMotion::getNumLeavingGhosts() == 0 && popupCallOutsOnDesktop() == 0; }, timeoutMs);
}

/** Closes what the test left open, puts every popover seam back to its default and expects that nothing was left:
 *  no call-out, no leaving picture, no modal component, no launcher or pointer override. */
inline void finishPopupTest() {
    using synth::ui::PopupMotion;
    closeOpenPopups();
    EXPECT_EQ(popupCallOutsOnDesktop(), 0) << "a call-out is still open";
    EXPECT_EQ(PopupMotion::getNumLeavingGhosts(), 0) << "a leaving picture is still on screen";
    EXPECT_EQ(juce::ModalComponentManager::getInstance()->getNumModalComponents(), 0) << "a modal component is left";

    PopupMotion::setAnimateOffScreenForTest(false);
    synth::testmode::setAppInFrontForTest(std::nullopt);
    synth::ui::test_hooks::popoverMouseForTest() = std::nullopt;
    synth::ui::test_hooks::popoverLauncherForTest() = nullptr;
    EXPECT_FALSE(PopupMotion::animatesOffScreenForTest());
}

} // namespace synth::test
