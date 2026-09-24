#pragma once

// Headless mapping-assistant helpers (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn,
// #orphan-controller): building an Assignment for a chosen control + target, and the two orphan-
// controller repairs (Re-link, Recreate). Pure data over the model types -- no store, no engine, no
// UI -- so the rules are unit-tested without a graph (Core, beside MidiRemoteLearnBinder.h).

#include "MidiRemote/RemoteModel.h"

#include <vector>

namespace synth::midi {

/** The assignment `control` (on `profile`) gets when the user maps it to `target`: a fresh id, the
 *  control's message/encoding/button-mode/name denormalised onto it, takeover "default", full range. */
Assignment makeAssignmentForControl(const ControllerProfile& profile, const Control& control, const Target& target);

struct RelinkResult {
    int matched = 0;   // assignments moved onto a control of the chosen profile
    int unmatched = 0; // assignments that had no control with the same message and stay orphaned
};

/** Re-link: every assignment in `assignments` that references `orphanProfileId` and whose message
 *  spec equals a control's message on `target` is repointed at that control (profile id, control id,
 *  and the denormalised name/encoding/button mode refreshed from it). The rest are left untouched --
 *  they stay orphaned, and the count says so. */
RelinkResult relinkAssignments(std::vector<Assignment>& assignments, const juce::String& orphanProfileId,
                               const ControllerProfile& target);

/** Recreate: mints a profile (fresh id, `name`, `input`) with ONE control per distinct message spec
 *  among the assignments that reference `orphanProfileId` (name, encoding and button mode from the
 *  first assignment carrying that spec; kind guessed from the message type; auto-laid-out), and
 *  repoints those assignments at it. Returns the new profile. */
ControllerProfile recreateProfileFromAssignments(std::vector<Assignment>& assignments,
                                                 const juce::String& orphanProfileId, const juce::String& name,
                                                 const ControllerProfile::Input& input);

} // namespace synth::midi
