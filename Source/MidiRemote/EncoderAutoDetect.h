#pragma once

// Headless encoder auto-detect (docs/control/midi-remote-ui.md#inspector-right, "Auto-detect...":
// "turn left... now right"). The ONE place an encoder's relative encoding is inferred -- the
// runtime never guesses (docs/control/midi-remote.md#data-model). Lives in Core: a state machine
// over raw 7-bit CC values, no UI.

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteModel.h"

#include <optional>
#include <vector>

namespace synth::midi {

class EncoderAutoDetect {
public:
    enum class Phase { idle, turnLeft, turnRight, done, undetermined };

    /** MESSAGE THREAD. Begins the left-turn phase for the encoder whose CC key is `spec`. */
    void start(const MessageSpec& spec);
    void cancel();

    /** Counts `event` into the current phase's sample set if the phase is turnLeft/turnRight and
     *  the event is a CC matching the started spec; returns whether it was counted. */
    bool feed(const RemoteEvent& event);

    /** Ends the current phase: turnLeft -> turnRight, turnRight -> done or undetermined. A no-op
     *  in any other phase. Returns the new phase. */
    Phase advance();

    Phase phase() const noexcept { return phase_; }
    /** Set only in Phase::done. */
    std::optional<Encoding> result() const noexcept { return result_; }
    /** Samples counted so far in the current phase. */
    int sampleCount() const noexcept;

    /** The pure decision over the raw CC values seen while turning left and while turning right;
     *  nullopt when the pattern matches no encoding (empty set, mixed values). See the .cpp for
     *  the rules. */
    static std::optional<Encoding> classify(const std::vector<int>& left, const std::vector<int>& right);

private:
    Phase phase_ = Phase::idle;
    MessageSpec spec_;
    std::vector<int> left_;
    std::vector<int> right_;
    std::optional<Encoding> result_;
};

} // namespace synth::midi
