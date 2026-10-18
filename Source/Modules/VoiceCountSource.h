#pragma once

// Read from the message thread: implementations return a value the audio thread published (an atomic), never walk voice
// arrays the audio thread writes.

namespace synth {

class VoiceCountSource {
public:
    virtual ~VoiceCountSource() = default;

    /** Voices sounding at the end of the last rendered block (held or ringing out). */
    virtual int displayVoiceCount() const noexcept = 0;
};

} // namespace synth
