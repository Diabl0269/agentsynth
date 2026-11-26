#pragma once

// ChatHistoryPicker.h -- the AI chat's history list: one compact, scrolling popover (the shared ModMatrixPicker list,
// which builds only the rows in view) with a title and date per saved chat and a "Clear my history" strip under it.
// Choosing a row opens that one chat; nothing else is built until then.
#include "AI/LocalHistoryStore.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace synth::chat_history {

struct Entry {
    juce::String id;
    juce::String title; // empty = "Untitled"
    juce::String date;  // already readable ("18 Aug 2026")
};

struct Hooks {
    std::function<void(const juce::String& id)> onOpen; // a row was chosen (click or Return)
    std::function<void()> onClear;                      // the "Clear my history" button was pressed
};

/** The list popover's content, ready for launchPopover. `onClear` runs after the list has closed. */
std::unique_ptr<juce::Component> makePicker(const std::vector<Entry>& entries, Hooks hooks);

// ---- Test seams (the content is a juce::Component the caller does not see the type of) ----
/** Rows the search lets through (every chat while it is empty), built or not. */
int listedCountForTest(juce::Component& picker);
/** Row components that exist right now: the rows in view plus a few, however long the list. */
int builtRowCountForTest(juce::Component& picker);
/** Texts of every row the search lets through, in order. */
std::vector<juce::String> visibleTitlesForTest(juce::Component& picker);
/** The second lines (the dates) of those rows. */
std::vector<juce::String> visibleDatesForTest(juce::Component& picker);
/** Sends a key to the list as typing in its search field would (Up, Down, Return, Escape). */
bool sendKeyForTest(juce::Component& picker, const juce::KeyPress& key);
/** Chooses the nth visible row as a click on it would. */
void chooseRowForTest(juce::Component& picker, int visibleIndex);
/** Presses the "Clear my history" button as a click on it would. */
void pressClearForTest(juce::Component& picker);
int heightForTest(juce::Component& picker);

} // namespace synth::chat_history
