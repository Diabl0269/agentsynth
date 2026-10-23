// RestoreWatch.h -- how a surface that refreshes from its model knows the refresh is an undo or redo: it asks at each
// sync with AppUndoManager::isRestoring() and getRestoreSerial(), and gets true for a sync inside a restore or the
// first one after a restore finished. Message thread only.
#pragma once

namespace synth::ui {

class RestoreWatch {
public:
    RestoreWatch() = default;
    /** Starts level with `serial`, so a restore that happened before the surface existed is not a restore to it. */
    explicit RestoreWatch(int serial) noexcept
        : seen_(serial) {}

    /** True when this sync belongs to a restore. A sync inside a restore expects the serial bump that follows it, so
     *  the next ordinary edit is not mistaken for one. */
    bool take(bool restoring, int serial) noexcept {
        const bool restored = restoring || serial != seen_;
        seen_ = restoring ? serial + 1 : serial;
        return restored;
    }

private:
    int seen_ = 0;
};

} // namespace synth::ui
