#pragma once

// A shorter copy of the vendored production edit-plan prompt for local (Ollama) models. The vendored text
// (generated/ProjectGenerate.g.h) stays whole and is never edited; the compact copy is made from it at run time by
// leaving out whole "### " sections a plain edit request does not use (full-song layout, sequencer steps, song key,
// pattern vocabulary, sound recipes and the longer examples). A small local model reads the prompt before every
// answer, so a third of the text is a third less waiting. Sections are matched by their heading text; a test fails
// when the vendored prompt no longer has one of them, which is the signal to review this list after a new prompt
// is vendored.

#include <juce_core/juce_core.h>

namespace synth::projectgenerate {

/** Heading texts (after "### ") of the sections Compact leaves out. */
const juce::StringArray& compactOmittedHeadings();

/** `prompt` without the "### " sections whose heading starts with one of `headings`; the text before the first
 *  heading and every other section are kept byte for byte. `missing`, when given, receives each heading that
 *  matched no section. */
juce::String omitSections(const juce::String& prompt, const juce::StringArray& headings,
                          juce::StringArray* missing = nullptr);

} // namespace synth::projectgenerate
