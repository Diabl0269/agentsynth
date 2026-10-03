#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

// SearchMatch.h -- the one matcher, ranker and highlight painter every search box uses (module
// library, Mod Matrix and the other pickers, Preferences, Shortcuts, MIDI Remote action picker,
// Mixer zones, MIDI destination picker). A box must not roll its own containsIgnoreCase: that is what
// made "osc 8" find "Oscillator 8" in one box and nothing in another.
//
// A query is split on whitespace into words. A text matches when EVERY word appears in it, ignoring
// case, in any order. A blank query matches everything (a box that wants "blank = no filter" gets it
// for free; one that wants "blank = show nothing extra" guards on the query itself).

/** True when every whitespace-separated word of `query` occurs in `text` (case-insensitive, any
 *  order). A blank query matches everything. */
bool searchMatches(const juce::String& text, const juce::String& query);

/** How well `text` matches `query`: -1 = no match, otherwise lower is better. Per word, the best
 *  occurrence counts: 0 = `text` starts with the word, 1 = the word starts at a word boundary (the
 *  previous character is not a letter or digit), 2 = it only occurs inside a word. The score is the
 *  worst (highest) over the words, so one weak word weakens the whole match. A blank query scores 0. */
int searchScore(const juce::String& text, const juce::String& query);

/** Inclusive-start, exclusive-end [start, start + length) range of a query hit inside a text. */
struct SearchSpan {
    int start = 0;
    int length = 0;
};

/** Every occurrence of every query word inside `text`, sorted by start, with overlapping or touching
 *  hits merged. Empty for a blank query or empty text. */
std::vector<SearchSpan> searchHighlightSpans(const juce::String& text, const juce::String& query);

/** Draws `text` in `normal` inside `bounds`; the letters `query` matched get a rounded `highlightFill`
 *  behind them and are drawn in `highlightText`. With no match it is a plain ellipsised drawText. This
 *  fill-plus-colour look is THE search highlight; no animation is involved. */
void drawSearchHighlightedText(juce::Graphics& g, const juce::String& text, const juce::String& query,
                               juce::Rectangle<int> bounds, const juce::Font& font, juce::Colour normal,
                               juce::Colour highlightFill, juce::Colour highlightText,
                               juce::Justification justification = juce::Justification::centredLeft);

} // namespace synth::ui
