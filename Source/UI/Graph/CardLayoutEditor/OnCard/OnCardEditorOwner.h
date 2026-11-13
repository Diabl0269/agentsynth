#pragma once

// Who keeps a running on-card layout editor alive: the GraphEditor it edits, through its own property
// set (the way ModuleCardLayoutBinding hangs the layout store there), so it dies with the canvas and
// only one session runs at a time. docs/layout/module-card-layout.md#editing-a-layout.

#include <memory>

class GraphEditor;

namespace synth::ui {

class CardLayoutOnCardEditor;

/** Makes `editor` the canvas's one running session. Its owner closes itself shortly after the session
 *  ends. Message thread only. */
void adoptOnCardLayoutEditor(GraphEditor& graph, std::unique_ptr<CardLayoutOnCardEditor> editor);

/** Ends the canvas's running session, if any, as Done does: call before opening the next one, so two
 *  sessions never overlap. */
void closeOnCardLayoutEditor(GraphEditor& graph);

} // namespace synth::ui
