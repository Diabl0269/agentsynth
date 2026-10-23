#pragma once

// What the Sounds view shows and says for a sound, a folder or a place. Pure, so the wording is unit-testable
// without a component or a native accessibility peer.

#include "Modules/Shapes/ShapeLibrary.h"
#include "SoundBrowser/SoundEntry.h"

namespace synth::ui {

/** "0.8 s", "12 s", "1:05". */
juce::String formatSoundLength(double seconds);

/** Whole bars of 4/4 a loop of `seconds` lasts at `tempo`; 0 when either is unknown. */
int soundBarCount(double tempo, double seconds);

/** The right-hand column of a row: the length of a one-shot, "96 . 2 bars" for a loop ("? . 2.0 s" with no tempo; the
 * dot is U+00B7), "instrument" for an .sfz. */
juce::String soundDetailText(const synth::soundbrowser::SoundEntry& entry);

/** "acoustic-snare-center, one-shot, 0.8 seconds, 2 of 23". */
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
