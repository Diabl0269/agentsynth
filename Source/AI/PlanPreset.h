#pragma once

#include <juce_core/juce_core.h>

namespace synth::planpreset {

// Turns the patch part of an AI edit plan into a preset (docs/ai/plan-preset.md): the answer's "nodes",
// "connections" and "modulations", checked as a fresh replace patch with AIStateMapper::validatePatch(trusted =
// false), built on a scratch graph through the same untrusted apply the live Apply uses (so node "state" and
// "uuid" are dropped exactly as there), and read back as an ordinary patch file. The live graph is never touched.

struct Result {
    bool ok = false;
    juce::String message; // on failure, the first problem found, in plain words
    juce::var preset;     // on success: the patch JSON a plain `.json` preset holds
};

/** True when the plan's patch part is a complete patch of its own: at least one node, and every check passes with
 *  nothing in the project to refer to. A plan that only edits modules already in the project (a merge patch) or
 *  whose patch needs a track its timeline ops build is not, and gets no Save as preset. Remembers its answers, so
 *  re-drawing a conversation does not re-check every plan. */
bool patchValidates(const juce::String& planJson);

/** Builds the preset from the plan. Modules the plan left without a position are laid out in columns by signal
 *  depth, so a loaded preset opens tidy; positions the plan gave are kept. */
Result buildPreset(const juce::String& planJson);

/** The file `rawName` would be saved to in `folder`: the name run through SnippetManager::sanitiseName (what track
 *  presets and snippets use), with " 2", " 3", ... added while a preset of that name exists, so saving never
 *  overwrites one. Empty when the name is nothing once sanitised. */
juce::File fileForName(const juce::File& folder, const juce::String& rawName);

/** Builds the preset and writes it to fileForName(folder, rawName). `written` is the file on success. */
Result saveAs(const juce::File& folder, const juce::String& rawName, const juce::String& planJson, juce::File& written);

} // namespace synth::planpreset
