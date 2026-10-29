#pragma once

// What the Sounds view shows and says for a sound, a folder or a place. Pure, so the wording is unit-testable
// without a component or a native accessibility peer.

#include "Modules/Shapes/ShapeLibrary.h"
#include "SoundBrowser/LoopStretch.h"
#include "SoundBrowser/SoundEntry.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"

namespace synth::ui {

/** "0.8 s", "12 s", "1:05". */
juce::String formatSoundLength(double seconds);

/** Whole bars of 4/4 a loop of `seconds` lasts at `tempo`; 0 when either is unknown. */
int soundBarCount(double tempo, double seconds);

/** The right-hand column of a row, always ending in the length: "0.8 s" for a one-shot, "120 BPM . 2 bars . 4.0 s" for
 * a loop (the dot is U+00B7; no bars when the length rounds to 0 bars, and just "4.0 s" with no tempo), "instrument"
 * for an .sfz. `maxParts` keeps only that many parts, dropping from the left (BPM, then bars), for a narrow row. */
juce::String soundDetailText(const synth::soundbrowser::SoundEntry& entry, int maxParts = 3);

/** What the footer says about a loop's key: "Key Am . song Dm" (the dot is U+00B7), "Key Am" while the song has no
 *  key; empty for a sound that is not a loop or whose key is unknown. */
juce::String soundKeyMetaText(const synth::soundbrowser::SoundEntry& entry, const juce::String& songKey);

/** The most a row `rowWidth` wide gives a sound's detail: 60% of the space between the margins, so the name keeps room
 *  at the narrowest sidebar. */
float soundDetailMaxWidth(int rowWidth);

/** soundDetailText with the fewest parts dropped (BPM first, then bars) that make it at most `maxWidth` wide in `font`;
 *  the length alone when even that is wider. */
juce::String soundDetailTextFitting(const synth::soundbrowser::SoundEntry& entry, const juce::Font& font,
                                    float maxWidth);

/** What the footer says under a loop about Match key: "Plays 2 semitones up, to D minor", "Already in D minor", "No key
 *  in the song", "This loop has no key"; empty when Match key is off. */
juce::String matchKeyText(const synth::soundbrowser::LoopTarget& target);

/** "acoustic-snare-center, one-shot, 0.8 seconds, 2 of 23"; a loop reads "dusty, loop, 120 BPM, 2 bars, 4.0 seconds,
 *  Am, 3 of 9" (the same fields as the row, in words). */
juce::String describeSoundForAccessibility(const synth::soundbrowser::SoundEntry& entry, int position, int count);
/** "Drums, folder, 24 sounds, 2 of 5". */
juce::String describeFolderForAccessibility(const juce::String& name, int soundCount, int position, int count);
/** "Core instruments, 214 sounds, 3 of 6"; a non-empty `status` ("Scanning", "Not found") replaces the count. */
juce::String describePlaceForAccessibility(const juce::String& name, int soundCount, int position, int count,
                                           const juce::String& status = {});
/** "Animals, not downloaded, 12 MB, press Return to download, 4 of 7" (or "downloading", with no button hint). */
juce::String describePackOfferForAccessibility(const juce::String& name, const juce::String& size, bool busy,
                                               int position, int count);
/** "Add folder, press Return to choose a folder, 7 of 8". */
juce::String describeAddFolderForAccessibility(int position, int count);
/** "ah, filter shape, built in, 3 of 20"; `kindWord` is "filter" or "lfo". Saved shapes read "saved by you". */
juce::String describeShapeForAccessibility(const juce::String& name, const juce::String& kindWord, bool factory,
                                           int position, int count);
/** "Filter shape, yours" or "LFO shape, factory": what the footer says under a shape's name. */
juce::String shapeKindAndSource(const synth::shapes::LibraryShape& shape);
/** What this platform calls its file browser: "Finder", "File Explorer", or "the file manager" on Linux. */
juce::String fileManagerName();
/** "Path, Places, Core instruments, Drums". */
juce::String describePathForAccessibility(const juce::StringArray& parts);

} // namespace synth::ui
