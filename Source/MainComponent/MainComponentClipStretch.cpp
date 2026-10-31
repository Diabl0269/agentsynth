// MainComponentClipStretch.cpp -- a Stretch edge drag of an audio clip (docs/timeline/clips.md#edge-sizing): the clip's
// audio is rendered at the new length by the loop stretcher, imported like any audio file, and the clip points at the
// copy in one undo step. The clip remembers the original asset and the cumulative ratio, so a second stretch renders
// from the original, and a return to 100% plays the original window again without a render. Older assets stay in the
// project, so undo plays them again.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentAudioImport.h"
#include "MainComponentInternal.h"
#include "SoundBrowser/LoopStretchFile.h"
#include "Timeline/TakeCaptureWriter.h"
#include "Timeline/TimelineDoc/ClipEdgeSizing.h"

namespace {

constexpr double kFallbackBpm = 120.0;

// What the drop did before audio could stretch, kept for when it cannot: the start follows a left edge, the length the
// drag. One undo step.
void resizeInstead(MainComponent& mc, synth::ClipId id, double newStartBeat, double newLengthBeats) {
    mc.getUndoManager().recordTimelineChange(mc.getTimelineDoc(), [&] {
        mc.getTimelineDoc().moveClip(id, newStartBeat);
        mc.getTimelineDoc().resizeClip(id, newLengthBeats);
    });
}

// The waveform sidecar a recorded take has, so the clip keeps its waveform after the drop. Both asset roots (the
// bundle, the app-data Recordings/ parent) sit two levels above the copy. A failure costs only the waveform.
void writePeaksFor(MainComponent& mc, const juce::String& stretchedRef) {
    const auto audioFile = mc.getAudioEngine().getAudioClipStreamer().resolveAssetRef(stretchedRef);
    if (!audioFile.existsAsFile())
        return;
    const auto root = audioFile.getParentDirectory().getParentDirectory();
    const auto peaksFile = root.getChildFile(detail::peaksRefForAssetRef(stretchedRef));
    peaksFile.getParentDirectory().createDirectory();
    synth::PeaksFile::writeForAudioFile(audioFile, peaksFile, synth::TakeCaptureWriter::kPeakBucketSize);
}

// The window to stretch, read through the clip's origin when that original file is still there; without it the clip's
// current asset is the best source left, and the new stretch starts a fresh origin from it.
std::optional<synth::AudioStretchWindow> windowFor(MainComponent& mc, const synth::Clip& clip, double newLength,
                                                   double secondsPerBeat) {
    bool useOrigin = clip.stretchOrigin.has_value();
    if (useOrigin)
        useOrigin =
            mc.getAudioEngine().getAudioClipStreamer().resolveAssetRef(clip.stretchOrigin->assetRef).existsAsFile();
    return synth::audioStretchWindow(clip, newLength, secondsPerBeat, useOrigin);
}

} // namespace

// The render blocks the message thread (about a second per minute of audio), which kMaxStretchWindowSeconds bounds. The
// copy is written to the temp folder first and imported from there, under the same policy as a dropped file.
void MainComponentAudioImport::stretchAudioClip(MainComponent& mc, synth::ClipId id, double newStartBeat,
                                                double newLengthBeats) {
    const auto* clip = mc.timelineDoc.getClip(id);
    if (clip == nullptr || clip->assetRef.isEmpty() || !(clip->lengthBeats > 0.0))
        return;
    const auto snap = mc.audioEngine.getTransport().getPositionSnapshot();
    const double bpm = snap.bpm > 0.0 ? snap.bpm : kFallbackBpm;
    const auto window = windowFor(mc, *clip, newLengthBeats, 60.0 / bpm);
    const juce::File source =
        window ? mc.audioEngine.getAudioClipStreamer().resolveAssetRef(window->sourceRef) : juce::File();
    const int percent = window ? juce::roundToInt(100.0 / window->tempoRatio) : 100;
    const juce::String name = clip->name;

    juce::String failure;
    if (!window)
        failure = "Could not stretch this clip";
    else if (!source.existsAsFile())
        failure = "The clip's audio file is missing";
    else if (!window->backToOriginal && window->lengthSeconds > synth::soundbrowser::kMaxStretchWindowSeconds)
        failure = "Clips over a minute do not stretch yet";
    // Back at 100% the original's own window is the answer: no render, so no copy and no loss.
    juce::String stretchedRef = failure.isEmpty() && window->backToOriginal ? window->sourceRef : juce::String();
    if (failure.isEmpty() && !window->backToOriginal) {
        const auto rendered =
            synth::soundbrowser::stretchedLoopCacheFolder()
                .getChildFile("Clips")
                .getChildFile(source.getFileNameWithoutExtension() + "-stretch" + juce::String(percent) + ".wav")
                .getNonexistentSibling();
        if (synth::soundbrowser::renderStretchedWindow(source, window->startSeconds, window->lengthSeconds,
                                                       window->tempoRatio, rendered))
            stretchedRef = importAudioAsset(mc, rendered);
        rendered.deleteFile();
        if (stretchedRef.isEmpty())
            failure = "Could not stretch the audio";
    }
    if (failure.isNotEmpty()) {
        resizeInstead(mc, id, newStartBeat, newLengthBeats);
        mc.statusBar.showMessage(failure + "; resized instead");
        return;
    }

    if (!window->backToOriginal)
        writePeaksFor(mc, stretchedRef);
    const double sourceStart = window->backToOriginal ? window->startSeconds : 0.0;
    mc.undoManager.recordTimelineChange(mc.timelineDoc, [&] {
        synth::applyAudioClipStretch(mc.timelineDoc, id, newStartBeat, newLengthBeats, stretchedRef, sourceStart,
                                     window->origin);
    });
    mc.statusBar.showMessage("Stretched " + name + " to " + juce::String(percent) + "%");
}
