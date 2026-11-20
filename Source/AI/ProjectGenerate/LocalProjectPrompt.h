#pragma once

// The edit-plan prompt a local (Ollama) model gets: a few KB owned by this app and written for small models, plus a
// module table cut to the modules the request can use. A small model re-reads the prompt before every answer, so
// every line here is a line it waits for; the output contract is enforced by the grammar-constrained schema
// (AIStateMapper::getProjectEditSchema) and the plan validator, not by prose.

#include <juce_core/juce_core.h>

namespace synth::projectgenerate {

/** The rules and two short worked examples, with the product name filled in. */
juce::String localProjectRules(const juce::String& productName);

/** The module types whose tables the prompt carries: those already in the project (`patchTypes`), a small core set
 *  that most sounds are built from, and any module the request names or hints at ("echo" -> Delay). Only types in
 *  `available` are returned; the order is `available`'s. */
juce::StringArray moduleTypesForRequest(const juce::String& request, const juce::StringArray& patchTypes,
                                        const juce::StringArray& available);

/** The whole local system prompt: rules, then the module table for `moduleTypes`. */
juce::String buildLocalProjectPrompt(const juce::String& productName, const juce::StringArray& moduleTypes);

} // namespace synth::projectgenerate
