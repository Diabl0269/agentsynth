#pragma once

#include "AI/SongSteps/SongSteps.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/ClipSelectionModel.h"
#include <optional>

/** The melody a song request carries: the notes of the one selected MIDI clip. Nothing when the selection is not
 *  exactly one clip, the clip is audio, or it holds no unmuted note. A tempo that is not positive reads as 120. */
std::optional<synth::songsteps::GivenMelody>
givenMelodyFromSelection(const synth::TimelineDoc& doc, const synth::ui::ClipSelectionModel& selection, double bpm);
