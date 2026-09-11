#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class AppUndoManager;

namespace synth {

class TimelineDoc;

/** The graph's Master node (docs/mixer.md §5.1), or nullptr. Master is a singleton by
 *  construction — ensureMasterNode() is the only thing that creates one — so the first found is it. */
juce::AudioProcessorGraph::Node* findMasterNode(juce::AudioProcessorGraph& graph);

/**
 * Splices Master into the graph (docs/mixer.md §5.1), doing everything ensureMasterNode() does
 * EXCEPT the undo transaction:
 *   - singleton: an existing Master is returned untouched;
 *   - it goes in front of the Rec Tap when there is one, else in front of Audio Output, so the
 *     chain always reads  strips -> Master -> Rec Tap -> Audio Output  whichever of Master and Rec
 *     Tap is spliced first (ensureMasterRecordTap re-routes Master's output into itself the same
 *     way it re-routes anything else);
 *   - every audio connection that fed that node's ch0/ch1 is re-routed into Master — into Mix when
 *     it comes from a Channel Strip, into Direct otherwise (MIDI and wider channels are left alone).
 *
 * NO UNDO — for a caller already inside its own undo transaction (T173a's
 * synth::buildDefaultAudioChannel, called from AppUndoManager::recordGraphTimelineAndMacroChange).
 * A caller with no transaction of its own should call ensureMasterNode() instead, which wraps this
 * in exactly the ONE recordCombinedChange step it always was.
 *
 * @param position canvas position for a newly created node (ignored when Master already exists).
 * @return the Master node (existing or newly spliced), or nullptr when the graph has no Audio
 *         Output to splice in front of.
 */
juce::AudioProcessorGraph::Node* spliceMasterNode(juce::AudioProcessorGraph& graph, juce::Point<int> position);

/**
 * Returns the graph's Master node, splicing one in first if there is none (docs/mixer.md §5.1) via
 * spliceMasterNode() above — node, uuid, position and the whole re-splice as ONE compound undo step
 * (AppUndoManager::recordCombinedChange).
 *
 * Only a USER ACTION calls this (the first channel's creation, P9-3) — never a project load, which
 * opens existing projects unchanged (docs/mixer.md §5.13).
 *
 * CALLER OBLIGATION: this changes the graph, so the caller must then run the app's reconcile /
 * publish seam (MainComponent::timelineChanged -> AudioEngine::publishTimeline, which also
 * recounts the mixer solo gate) and refresh the editor (GraphEditor::updateComponents) — see the
 * hook inventory in docs/architecture.md §8.
 *
 * @param position canvas position for a newly created node (ignored when Master already exists).
 * @return the Master node, or nullptr when the graph has no Audio Output to splice in front of.
 */
juce::AudioProcessorGraph::Node* ensureMasterNode(juce::AudioProcessorGraph& graph, AppUndoManager& undoManager,
                                                  TimelineDoc& doc, juce::Point<int> position);

} // namespace synth
