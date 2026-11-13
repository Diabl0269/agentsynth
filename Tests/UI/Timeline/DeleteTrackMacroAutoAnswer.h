// DeleteTrackMacroAutoAnswer.h: a test that deletes a track through the row menu (or a key) while the track's macro
// goes with it meets the Remove macro / Keep macro question. This answers it "Remove macro" at once, so the test keeps
// proving what it proved before the question existed; a test about the question itself installs its own hook.
#pragma once

#include "UI/Timeline/DeleteTrackConfirm.h"
#include <type_traits>

struct DeleteTrackMacroAutoRemove {
    // Restores what was installed before, so a second rig (a project reopened beside the first) leaves the first's
    // answer in place when it goes away.
    DeleteTrackMacroAutoRemove()
        : previous_(synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest()) {
        synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest() = [](const auto&, auto done) {
            done(synth::ui::DeleteTrackMacroChoice::removeMacro, false);
        };
    }
    ~DeleteTrackMacroAutoRemove() { synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest() = previous_; }
    DeleteTrackMacroAutoRemove(const DeleteTrackMacroAutoRemove&) = delete;
    DeleteTrackMacroAutoRemove& operator=(const DeleteTrackMacroAutoRemove&) = delete;

private:
    std::remove_reference_t<decltype(synth::ui::test_hooks::deleteTrackMacroConfirmHookForTest())> previous_;
};
