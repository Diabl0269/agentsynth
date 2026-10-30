#pragma once

// AutomationHiding.h: keeps an automation run (the test-mode app, the Tests binary) off the user's screen.
#include <optional>

namespace synth::testmode {

/** Call once at startup, before the first window exists. Windows made afterwards are invisible and click-through, and
 *  the process never takes focus. On macOS it also drops the Dock icon and keeps the process from being throttled
 *  while hidden. */
void hideAutomationWindows();

/** True once hideAutomationWindows() has run (macOS: windows are invisible; elsewhere nothing is hidden). */
bool automationWindowsAreHidden();

/** juce::Process::isForegroundProcess(), except that a hidden automation process counts as foreground: it is never
 *  the active app, yet its pop-ups must stay open. */
bool appIsInFront();

/** Test seam: makes appIsInFront() answer `inFront` (true/false), standing in for the app being deactivated; nullopt
 *  puts the real answer back. */
void setAppInFrontForTest(std::optional<bool> inFront);

} // namespace synth::testmode
