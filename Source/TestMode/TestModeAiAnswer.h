#pragma once

#include "TestMode/TestModeCommands.h"

// TestModeAiAnswer.h: `aiAnswer` (docs/development/test-mode.md#aianswer). A test-mode launch has no AI provider, so
// no assistant card ever appears. `aiAnswer {text}` puts one assistant answer into the chat as if the provider had
// sent `text` (a patch or edit-plan JSON), so its edit-plan card, previewed against the open project, can be driven.
namespace synth::testmode {

/** Adds aiAnswer. */
void registerAiAnswerCommands(CommandTable& table);

} // namespace synth::testmode
