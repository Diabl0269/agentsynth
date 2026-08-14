#include "TransportService.h"

#include <algorithm>

namespace synth {

TransportService::TransportService() { publishSnapshot(); }

// -- Message-thread API -------------------------------------------------------

bool TransportService::play() {
    Command c;
    c.type = Command::Type::Play;
    return postCommand(c);
}

bool TransportService::stop() {
    Command c;
    c.type = Command::Type::Stop;
    return postCommand(c);
}

bool TransportService::locateBeat(double beat) {
    Command c;
    c.type = Command::Type::LocateBeat;
    c.a = std::max(0.0, beat);
    return postCommand(c);
}

bool TransportService::setLoop(double startBeat, double endBeat, bool enabled) {
    Command c;
    c.type = Command::Type::SetLoop;
    c.a = std::max(0.0, startBeat);
    c.b = std::max(0.0, endBeat);
    c.flag = enabled && (c.b - c.a >= kMinLoopLengthBeats);
    return postCommand(c);
}

bool TransportService::setBpm(double newBpm) {
    Command c;
    c.type = Command::Type::SetBpm;
    c.a = std::clamp(newBpm, kMinBpm, kMaxBpm);
    return postCommand(c);
}

bool TransportService::setTimeSignature(int numerator, int denominator) {
    if (numerator < 1 || numerator > 64)
        return false;
    // Restrict to note-value denominators; anything else is a caller bug.
    if (denominator != 1 && denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16 &&
        denominator != 32)
        return false;
    Command c;
    c.type = Command::Type::SetTimeSignature;
    c.i = numerator;
    c.j = denominator;
    return postCommand(c);
}

bool TransportService::postCommand(const Command& command) noexcept {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1)
        return false; // full: drop rather than block — the caller may re-post
    commandSlots[(size_t)start1] = command;
    fifo.finishedWrite(1);
    return true;
}

// -- Audio-thread API ---------------------------------------------------------

void TransportService::prepare(double sampleRate, int maxBlockSize) {
    juce::ignoreUnused(maxBlockSize);
    tempoMap.setSampleRate(sampleRate);
    // Musical position is canonical: keep the beat, re-derive the sample.
    samplePosition = tempoMap.sampleFromBeat(ppqPosition);
    currentBlock = {};
    currentBlock.sampleRate = sampleRate;
    publishSnapshot();
}

void TransportService::drainCommands() noexcept {
    for (;;) {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 + size2 < 1)
            return;
        applyCommand(commandSlots[(size_t)(size1 > 0 ? start1 : start2)]);
        fifo.finishedRead(1);
    }
}

void TransportService::applyCommand(const Command& command) noexcept {
    switch (command.type) {
    case Command::Type::Play:
        playing = true;
        break;
    case Command::Type::Stop:
        playing = false;
        break;
    case Command::Type::LocateBeat:
        ppqPosition = command.a;
        samplePosition = tempoMap.sampleFromBeat(ppqPosition);
        break;
    case Command::Type::SetLoop:
        loopStartPpq = command.a;
        loopEndPpq = command.b;
        looping = command.flag;
        break;
    case Command::Type::SetBpm:
        // Beats are canonical: the musical position survives a tempo change,
        // the sample position is re-derived under the new map.
        tempoMap.setBpm(command.a);
        samplePosition = tempoMap.sampleFromBeat(ppqPosition);
        break;
    case Command::Type::SetTimeSignature:
        timeSigNumerator = command.i;
        timeSigDenominator = command.j;
        break;
    }
}

const BlockTimeInfo& TransportService::tick(int numSamples) {
    drainCommands();

    currentBlock.blockStartSample = samplePosition;
    currentBlock.numSamples = numSamples;
    currentBlock.startPpq = ppqPosition;
    currentBlock.bpm = tempoMap.getBpm();
    currentBlock.sampleRate = tempoMap.getSampleRate();
    currentBlock.timeSigNumerator = timeSigNumerator;
    currentBlock.timeSigDenominator = timeSigDenominator;
    currentBlock.playing = playing;
    currentBlock.looping = looping;
    currentBlock.loopStartPpq = loopStartPpq;
    currentBlock.loopEndPpq = loopEndPpq;
    currentBlock.loopWrapSample = -1;

    publishSnapshot(); // block-start state is what this block sounds like

    if (playing && numSamples > 0) {
        std::int64_t endSample = samplePosition + numSamples;
        currentBlock.endPpq = tempoMap.beatFromSample(endSample);

        if (looping && loopEndPpq > loopStartPpq) {
            const auto loopStartSample = tempoMap.sampleFromBeat(loopStartPpq);
            const auto loopEndSample = tempoMap.sampleFromBeat(loopEndPpq);
            const auto loopLength = std::max<std::int64_t>(1, loopEndSample - loopStartSample);

            // Wrap only when this block crosses the loop end from inside the loop;
            // a locate past the end just plays on.
            if (samplePosition < loopEndSample && endSample >= loopEndSample) {
                const auto wrapAt = loopEndSample - samplePosition; // in [1, numSamples]
                if (wrapAt < numSamples)
                    currentBlock.loopWrapSample = (int)wrapAt;
                endSample = loopStartSample + (endSample - loopEndSample) % loopLength;
            }
        }

        samplePosition = endSample;
        ppqPosition = tempoMap.beatFromSample(samplePosition);
    } else {
        currentBlock.endPpq = ppqPosition;
    }

    return currentBlock;
}

// -- Cross-thread publication ---------------------------------------------------

void TransportService::publishSnapshot() noexcept {
    sharedVersion.fetch_add(1); // odd: write in flight
    shared.ppq.store(ppqPosition);
    shared.samplePosition.store(samplePosition);
    shared.bpm.store(tempoMap.getBpm());
    shared.sampleRate.store(tempoMap.getSampleRate());
    shared.timeSigNumerator.store(timeSigNumerator);
    shared.timeSigDenominator.store(timeSigDenominator);
    shared.playing.store(playing);
    shared.looping.store(looping);
    shared.loopStartPpq.store(loopStartPpq);
    shared.loopEndPpq.store(loopEndPpq);
    sharedVersion.fetch_add(1); // even: write complete
}

TransportService::PositionSnapshot TransportService::getPositionSnapshot() const noexcept {
    PositionSnapshot out;
    for (;;) {
        const auto before = sharedVersion.load();
        if ((before & 1u) != 0u)
            continue; // writer mid-flight; it finishes within the same tick
        out.ppq = shared.ppq.load();
        out.samplePosition = shared.samplePosition.load();
        out.bpm = shared.bpm.load();
        out.sampleRate = shared.sampleRate.load();
        out.timeSigNumerator = shared.timeSigNumerator.load();
        out.timeSigDenominator = shared.timeSigDenominator.load();
        out.playing = shared.playing.load();
        out.looping = shared.looping.load();
        out.loopStartPpq = shared.loopStartPpq.load();
        out.loopEndPpq = shared.loopEndPpq.load();
        if (sharedVersion.load() == before)
            return out;
    }
}

juce::Optional<juce::AudioPlayHead::PositionInfo> TransportService::getPosition() const {
    const auto snap = getPositionSnapshot();
    juce::AudioPlayHead::PositionInfo info;
    info.setBpm(snap.bpm);
    info.setTimeSignature(juce::AudioPlayHead::TimeSignature{snap.timeSigNumerator, snap.timeSigDenominator});
    info.setPpqPosition(snap.ppq);
    info.setTimeInSamples(snap.samplePosition);
    if (snap.sampleRate > 0.0)
        info.setTimeInSeconds((double)snap.samplePosition / snap.sampleRate);
    info.setIsPlaying(snap.playing);
    info.setIsLooping(snap.looping);
    if (snap.looping)
        info.setLoopPoints(juce::AudioPlayHead::LoopPoints{snap.loopStartPpq, snap.loopEndPpq});

    const double beatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
    if (beatsPerBar > 0.0) {
        const double barCount = std::floor(snap.ppq / beatsPerBar);
        info.setPpqPositionOfLastBarStart(barCount * beatsPerBar);
        info.setBarCount((std::int64_t)barCount);
    }
    return info;
}

} // namespace synth
