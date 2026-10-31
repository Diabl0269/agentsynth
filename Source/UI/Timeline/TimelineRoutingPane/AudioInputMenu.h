#pragma once

#include "Timeline/TimelineDoc/TimelineTypes.h"
#include "Timeline/TrackAudioInput.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// AudioInputMenu.h (docs/timeline/tracks.md#the-input-row): the audio track Input row's words and menu, as pure
// functions of the track's choice and the inputs on offer, so a test reads exactly what the pane shows.
namespace synth {
class TimelineDoc;
}

namespace synth::ui {

struct AudioInputMenuIds {
    static constexpr int kNoInput = 1, kWholeMix = 2, kAudioSettings = 3, kTurnOnInputs = 4, kNotConnected = 5;
    static constexpr int kTrackRemoved = 6;
    static constexpr int kFirstMono = 100;   // + 0-based input
    static constexpr int kFirstStereo = 200; // + 0-based first input of the pair
    static constexpr int kFirstTrack = 300;  // + index into the listed track sources
};

/** One track the "Another track" submenu offers: any other track that is not an Automation one. */
struct AudioInputTrackSource {
    synth::TrackId id;
    juce::String name;
    bool hasChannel = false; // its sound reaches a mixer channel; without one it cannot be recorded
};

/** The submenu's rows for the Audio track `target`: every track but `target` and the Automation ones, in timeline
 *  order. `hasChannel` answers whether a track reaches a mixer channel. */
std::vector<AudioInputTrackSource> listAudioInputTrackSources(const synth::TimelineDoc& doc, synth::TrackId target,
                                                              const std::function<bool(synth::TrackId)>& hasChannel);

/** What the Input combo says, and how. */
struct AudioInputRowText {
    juce::String text;    // "No input", "In 1 (mono)", "In 1 + 2", "Whole mix", "In 1 (<device> not connected)",
                          // "Vocal mic (In 1)", "Vocal mic (not on this interface)", a source track's name,
                          // "(track removed)"
    bool muted = false;   // No input: drawn in the muted text colour
    bool warning = false; // the choice cannot record now
    juce::String note;    // the warning's explanation under the combo; empty when there is nothing to explain
};

AudioInputRowText describeAudioInput(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status,
                                     const std::vector<AudioInputTrackSource>& sources = {});

/** The combo's menu (ids in AudioInputMenuIds): No input; the device's mono inputs, then its stereo pairs (or, with
 *  its inputs off, a line saying so and "Turn on its inputs"); "Another track" with one row per source (one with no
 *  mixer channel is disabled); the whole mix; "Audio settings..." (not Hosted). */
juce::PopupMenu buildAudioInputMenu(const synth::AudioInputChoice& choice, const synth::AudioInputStatus& status,
                                    const std::vector<AudioInputTrackSource>& sources = {});

/** What a menu id asks for. A Pick carries the new choice; anything unknown, or the ticked not-connected entry, is
 *  Nothing. */
struct AudioInputMenuAction {
    enum class Kind { Nothing, Pick, TurnOnInputs, OpenAudioSettings };
    Kind kind = Kind::Nothing;
    synth::AudioInputChoice choice;
};
AudioInputMenuAction resolveAudioInputMenuChoice(int menuId, const synth::AudioInputStatus& status,
                                                 const std::vector<AudioInputTrackSource>& sources = {});

} // namespace synth::ui
