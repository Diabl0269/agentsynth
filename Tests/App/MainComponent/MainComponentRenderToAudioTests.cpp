// "Render to audio track" end to end through MainComponent (docs/timeline/render-to-audio.md): the render runs on the
// message thread behind its timer, the files land in the project like takes, and ONE undo step adds the audio tracks
// and mutes what they replace. The null test proves the point of the feature: the rendered track is not heard
// differently from the source it replaces.

#include "../../Timeline/TakeFileTestHelpers.h"
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "MainComponent/MainComponentRenderToAudio.h"
#include "Mixer/TrackChannelLink.h"
#include "ProjectBundle.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/PeaksFile.h"
#include "Transport/BounceExporter.h"
#include "Transport/StemSession.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UserSettings.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {

using synth::test::readTakeWav;
using synth::ui::RenderSpan;

class MinimalProviderRT : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "RenderMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"mock-model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{true, "Mock response.", {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "mock-model";
    int requestTimeoutMs = 240000;
};

// Delivers a key the way JUCE does: to the focused component, then up its parents until one handles it.
bool dispatchKeyFrom(juce::Component& focused, const juce::KeyPress& key) {
    for (auto* component = &focused; component != nullptr; component = component->getParentComponent())
        if (component->keyPressed(key))
            return true;
    return false;
}

} // namespace

class RenderToAudioTest : public ::testing::Test {
protected:
    void resetKeys() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            s->setValue("librarySidebarVisible", "1");
            s->setValue("aiPanelVisible", "0");
            s->setValue("minimapVisible", "1");
            s->setValue("bottomDockVisible", "0");
            s->setValue("timelineCountInBars", 0);
            s->saveIfNeeded();
        }
    }

    void SetUp() override {
        resetKeys();
        MainComponentRenderToAudio::setProgressDialogEnabledForTest(false);
        bundleDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("agentsynth_render_audio_" + juce::Uuid().toString() + ".agsproj");
        bundleDir.deleteRecursively();
        mc = std::make_unique<MainComponent>(std::make_unique<MinimalProviderRT>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->getGraphEditor().newPatch();
        mc->getGraphEditor().addModuleAtCanvasPosition("Poly MIDI", {200, 200}, {});
    }
    void TearDown() override {
        mc.reset();
        MainComponentRenderToAudio::setProgressDialogEnabledForTest(true);
        resetKeys();
        bundleDir.deleteRecursively();
        for (const auto& file : recordingsToClean)
            file.deleteFile();
    }

    // An Oscillator instrument track holding one clip over beats 0..8 with two notes, so the render has sound and gaps.
    synth::TrackId addLead(int firstPitch = 60) {
        auto& doc = mc->getTimelineDoc();
        const auto before = doc.getTracks().size();
        mc->simulateAddInstrumentTrackClick(synth::ui::TimelinePanelComponent::kAddInstrumentOscillatorMenuId);
        EXPECT_EQ(doc.getTracks().size(), before + 1);
        const auto track = doc.getTracks().back().id;
        const auto clip = doc.addClip(track, 0.0, 8.0, "Riff");
        for (const double beat : {0.0, 4.0}) {
            synth::MidiNote note;
            note.startBeat = beat;
            note.lengthBeats = 2.0;
            note.pitch = firstPitch + (beat > 0.0 ? 7 : 0);
            EXPECT_TRUE(doc.addNote(clip, note).isValid());
        }
        return track;
    }

    // Starts a render and pumps the message loop until it and its commit are done.
    bool render(const std::vector<RenderSpan>& spans) {
        if (!MainComponentRenderToAudio::request(*mc, spans))
            return false;
        const auto deadline = juce::Time::getMillisecondCounter() + 60000;
        while (!MainComponentRenderToAudio::isIdleForTest(*mc) && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        return MainComponentRenderToAudio::isIdleForTest(*mc);
    }

    int stripCount() { return (int)synth::collectStemStrips(mc->getAudioEngine().getGraph()).size(); }
    ChannelStripModule* stripOf(synth::TrackId track) {
        const auto link = synth::resolveTrackChannelLink(mc->getAudioEngine().getGraph(), mc->getTimelineDoc(), track);
        auto* node = link.hasChannel ? mc->getAudioEngine().getGraph().getNodeForId(link.stripId) : nullptr;
        return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    }
    static void setParam(juce::AudioProcessor& processor, const juce::String& id, float value) {
        for (auto* p : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
                if (ranged->getParameterID() == id)
                    ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
    }
    juce::String status() { return mc->getStatusBar().getTransientMessageForTest(); }

    std::unique_ptr<MainComponent> mc;
    juce::File bundleDir;
    std::vector<juce::File> recordingsToClean;
};

TEST_F(RenderToAudioTest, AddsTheTrackRightBelowItsSourceAsOneUndoStep) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    auto& doc = mc->getTimelineDoc();
    const auto leadName = doc.getTrack(lead)->name;
    const auto leadColour = doc.getTrack(lead)->colourArgb;
    const int strips = stripCount();
    mc->getUndoManager().clearUndoHistory();

    ASSERT_TRUE(render({{lead, 2.0, 6.0}}));
    EXPECT_EQ(status(), "Rendered 1 track to audio");

    ASSERT_EQ(doc.getTracks().size(), 2u);
    const auto& rendered = doc.getTracks()[1];
    EXPECT_EQ(rendered.kind, synth::TrackKind::Audio);
    EXPECT_EQ(rendered.name, leadName + " audio");
    EXPECT_EQ(rendered.colourArgb, leadColour);
    EXPECT_EQ(rendered.audioInput.kind, synth::AudioInputChoice::Kind::None);
    ASSERT_EQ(rendered.clips.size(), 1u);
    const auto& clip = rendered.clips[0];
    EXPECT_DOUBLE_EQ(clip.startBeat, 2.0);
    EXPECT_DOUBLE_EQ(clip.lengthBeats, 4.0);
    EXPECT_EQ(clip.name, leadName);
    EXPECT_EQ(clip.assetRef, "Audio/render-1.wav");
    EXPECT_DOUBLE_EQ(clip.sourceStartSeconds, 0.0);
    EXPECT_EQ(stripCount(), strips + 1) << "the new track has its own mixer channel";

    const auto wav = readTakeWav(bundleDir.getChildFile(clip.assetRef));
    ASSERT_TRUE(wav.ok);
    EXPECT_EQ(wav.numChannels, 2);
    const double secondsPerBeat = 0.5; // 120 bpm
    EXPECT_GE(wav.lengthInSamples, (juce::int64)((4.0 * secondsPerBeat + 2.0) * wav.sampleRate - 1024))
        << "the 2 second tail is in the file past the clip's end";
    synth::PeaksFile::Data peaks;
    EXPECT_TRUE(synth::PeaksFile::read(bundleDir.getChildFile("Peaks/render-1.agpk"), peaks))
        << "the waveform draws at once";
    EXPECT_FALSE(peaks.buckets.empty());

    // The source is split at both edges and what lies inside is muted.
    const auto& sourceClips = doc.getTrack(lead)->clips;
    ASSERT_EQ(sourceClips.size(), 3u);
    int muted = 0;
    for (const auto& each : sourceClips) {
        const bool inside = each.startBeat >= 2.0 && each.startBeat + each.lengthBeats <= 6.0;
        EXPECT_EQ(each.muted, inside) << "clip at " << each.startBeat;
        muted += each.muted ? 1 : 0;
    }
    EXPECT_EQ(muted, 1);

    mc->simulateUndoClick();
    EXPECT_EQ(doc.getTracks().size(), 1u);
    EXPECT_EQ(doc.getTrack(lead)->clips.size(), 1u) << "undo un-splits the source";
    EXPECT_FALSE(doc.getTrack(lead)->clips[0].muted);
    EXPECT_EQ(stripCount(), strips) << "and removes the new channel";
    EXPECT_FALSE(mc->getUndoManager().canUndo()) << "the whole render was ONE undo step";

    mc->simulateRedoClick();
    EXPECT_EQ(doc.getTracks().size(), 2u);
    EXPECT_EQ(doc.getTrack(lead)->clips.size(), 3u);
    EXPECT_EQ(stripCount(), strips + 1);
}

TEST_F(RenderToAudioTest, ARangeOverTwoTracksMakesATrackUnderEachSource) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto a = addLead(60);
    const auto b = addLead(64);
    auto& doc = mc->getTimelineDoc();
    ASSERT_TRUE(render({{a, 0.0, 8.0}, {b, 0.0, 8.0}}));
    EXPECT_EQ(status(), "Rendered 2 tracks to audio");

    ASSERT_EQ(doc.getTracks().size(), 4u);
    EXPECT_EQ(doc.getTracks()[0].id, a);
    EXPECT_EQ(doc.getTracks()[1].name, doc.getTracks()[0].name + " audio");
    EXPECT_EQ(doc.getTracks()[2].id, b);
    EXPECT_EQ(doc.getTracks()[3].name, doc.getTracks()[2].name + " audio");
    EXPECT_NE(doc.getTracks()[1].clips[0].assetRef, doc.getTracks()[3].clips[0].assetRef);
}

TEST_F(RenderToAudioTest, ATrackWithoutAMixerChannelIsSkippedAndNoneLeftRefuses) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    auto& doc = mc->getTimelineDoc();
    const auto loose = doc.addTrack(synth::TrackKind::Midi, "Loose");
    EXPECT_EQ(MainComponentRenderToAudio::renderableTracks(*mc, {lead, loose}), std::vector<synth::TrackId>{lead});
    EXPECT_TRUE(MainComponentRenderToAudio::renderableTracks(*mc, {loose}).empty());

    EXPECT_FALSE(MainComponentRenderToAudio::request(*mc, {{loose, 0.0, 4.0}}));
    EXPECT_EQ(status(), "Nothing to render: no track in the selection has a mixer channel");
    EXPECT_TRUE(MainComponentRenderToAudio::isIdleForTest(*mc));
    EXPECT_EQ(doc.getTracks().size(), 2u);

    ASSERT_TRUE(render({{lead, 0.0, 4.0}, {loose, 0.0, 4.0}}));
    EXPECT_EQ(status(), "Rendered 1 track to audio") << "the channel-less track is skipped";
}

TEST_F(RenderToAudioTest, IsRefusedWhileRecording) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    ASSERT_TRUE(mc->getTimelineDoc().setTrackArmed(lead, true));
    mc->getTimelinePanel().getTransportBar().getRecordButton().onClick();
    ASSERT_TRUE(mc->isRecordingActiveForTest());

    EXPECT_FALSE(MainComponentRenderToAudio::request(*mc, {{lead, 0.0, 4.0}}));
    EXPECT_EQ(status(), "Can't render to audio while recording");
    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), 1u);
    mc->getTimelinePanel().getTransportBar().getRecordButton().onClick();
}

TEST_F(RenderToAudioTest, IsRefusedWhileAnotherRenderRuns) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    ASSERT_TRUE(MainComponentRenderToAudio::request(*mc, {{lead, 0.0, 8.0}}));
    EXPECT_FALSE(MainComponentRenderToAudio::isIdleForTest(*mc))
        << "autosave, export and graph edits see the same busy flag as an export";
    EXPECT_FALSE(MainComponentRenderToAudio::request(*mc, {{lead, 0.0, 8.0}}));
    EXPECT_EQ(status(), "Can't render to audio while an export or another render is running");
    MainComponentRenderToAudio::cancelForTest(*mc);
    const auto deadline = juce::Time::getMillisecondCounter() + 30000;
    while (!MainComponentRenderToAudio::isIdleForTest(*mc) && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_TRUE(MainComponentRenderToAudio::isIdleForTest(*mc));
}

TEST_F(RenderToAudioTest, CancelAddsNothingLeavesNoFilesAndRestoresTheMute) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    auto* strip = stripOf(lead);
    ASSERT_NE(strip, nullptr);
    strip->setMuted(true);
    mc->getUndoManager().clearUndoHistory();

    ASSERT_TRUE(MainComponentRenderToAudio::request(*mc, {{lead, 0.0, 8.0}}));
    MainComponentRenderToAudio::cancelForTest(*mc);
    const auto deadline = juce::Time::getMillisecondCounter() + 30000;
    while (!MainComponentRenderToAudio::isIdleForTest(*mc) && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    EXPECT_EQ(status(), "Render cancelled");
    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), 1u);
    EXPECT_FALSE(mc->getUndoManager().canUndo());
    EXPECT_TRUE(strip->isMuted()) << "the source's mute is back";
    EXPECT_EQ(strip->getLiveTapBuffer(), nullptr);
    EXPECT_EQ(bundleDir.getChildFile("Audio").findChildFiles(juce::File::findFiles, false, "render-*").size(), 0);
}

TEST_F(RenderToAudioTest, AMutedSourceRendersItsSoundAndStaysMuted) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    auto* strip = stripOf(lead);
    ASSERT_NE(strip, nullptr);
    strip->setMuted(true);

    ASSERT_TRUE(render({{lead, 0.0, 8.0}}));
    const auto wav = readTakeWav(bundleDir.getChildFile("Audio/render-1.wav"));
    ASSERT_TRUE(wav.ok);
    EXPECT_GT(wav.audio.getMagnitude(0, 0, (int)wav.lengthInSamples), 0.01f)
        << "the muted source is heard in the render";
    EXPECT_TRUE(strip->isMuted());
}

TEST_F(RenderToAudioTest, AnUnsavedProjectRendersIntoRecordingsAndTheFirstSaveAdoptsIt) {
    const auto lead = addLead();
    auto& doc = mc->getTimelineDoc();
    ASSERT_TRUE(render({{lead, 0.0, 4.0}}));
    const auto ref = doc.getTracks()[1].clips[0].assetRef;
    EXPECT_TRUE(ref.startsWith("Recordings/render-")) << ref;
    const auto recorded = synth::userSettingsRootDirectory().getChildFile(ref);
    recordingsToClean = {recorded, recorded.withFileExtension("agpk")};
    EXPECT_TRUE(recorded.existsAsFile());
    EXPECT_TRUE(recorded.withFileExtension("agpk").existsAsFile());

    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto adopted = doc.getTracks()[1].clips[0].assetRef;
    EXPECT_TRUE(adopted.startsWith("Audio/render-")) << adopted;
    EXPECT_TRUE(bundleDir.getChildFile(adopted).existsAsFile());
    EXPECT_TRUE(bundleDir.getChildFile("Peaks")
                    .getChildFile(juce::File(adopted).getFileNameWithoutExtension() + ".agpk")
                    .existsAsFile());
}

TEST_F(RenderToAudioTest, ASavedAndReloadedProjectRestoresTheRenderedTrackAndItsChannel) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    ASSERT_TRUE(render({{lead, 2.0, 6.0}}));
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    ASSERT_TRUE(mc->openProjectForTest(bundleDir));

    auto& doc = mc->getTimelineDoc();
    ASSERT_EQ(doc.getTracks().size(), 2u);
    const auto& rendered = doc.getTracks()[1];
    EXPECT_EQ(rendered.kind, synth::TrackKind::Audio);
    ASSERT_EQ(rendered.clips.size(), 1u);
    EXPECT_EQ(rendered.clips[0].assetRef, "Audio/render-1.wav");
    EXPECT_TRUE(bundleDir.getChildFile(rendered.clips[0].assetRef).existsAsFile());
    EXPECT_TRUE(synth::resolveTrackChannelLink(mc->getAudioEngine().getGraph(), doc, rendered.id).hasChannel)
        << "the macro-boxed channel came back with the track";
    int muted = 0;
    for (const auto& clip : doc.getTracks()[0].clips)
        muted += clip.muted ? 1 : 0;
    EXPECT_EQ(muted, 1) << "the source's muted clip came back";
}

TEST_F(RenderToAudioTest, TheShortcutTriggersTheSameRender) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    auto& lane = mc->getTimelinePanel().getClipLaneArea();
    lane.setRange(lead, 2.0, lead, 6.0);
    ASSERT_TRUE(dispatchKeyFrom(lane, ShortcutManager().getBinding("timelineRenderToAudioTrack")));
    const auto deadline = juce::Time::getMillisecondCounter() + 60000;
    while (!MainComponentRenderToAudio::isIdleForTest(*mc) && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_EQ(status(), "Rendered 1 track to audio");
    EXPECT_EQ(mc->getTimelineDoc().getTracks().size(), 2u);
}

// The null test: the rendered track is not heard differently from the source it replaces. The same bars are bounced
// twice through the master, once with the source playing and the rendered track muted, once the way the command leaves
// the project (source clips muted, rendered track playing); the difference must vanish under -60 dBFS.
TEST_F(RenderToAudioTest, NullTestTheRenderedTrackSoundsLikeTheSourceItReplaces) {
    ASSERT_TRUE(mc->saveProjectForTest(bundleDir));
    const auto lead = addLead();
    auto* strip = stripOf(lead);
    ASSERT_NE(strip, nullptr);
    setParam(*strip, "gain", -4.0f); // the render includes the channel's fader and pan
    setParam(*strip, "pan", 0.3f);

    // The oscillator's free-running phase settles after a pass; warm it up so the render and the later bounces
    // start every note from the same state.
    for (int pass = 0; pass < 2; ++pass) {
        synth::BounceOptions warm;
        warm.startBeat = 0.0;
        warm.endBeat = 8.0;
        warm.sampleRate = 44100.0;
        warm.bitDepth = 32;
        const auto warmFile = bundleDir.getSiblingFile(bundleDir.getFileName() + "_warm.wav");
        ASSERT_TRUE(synth::BounceExporter::bounce(mc->getAudioEngine(), warmFile, warm).ok);
        warmFile.deleteFile();
    }

    ASSERT_TRUE(render({{lead, 0.0, 8.0}}));
    auto& doc = mc->getTimelineDoc();
    ASSERT_EQ(doc.getTracks().size(), 2u);
    const auto rendered = doc.getTracks()[1].id;

    const auto renderedWav = readTakeWav(bundleDir.getChildFile(doc.getTracks()[1].clips[0].assetRef));
    ASSERT_TRUE(renderedWav.ok);
    synth::BounceOptions options;
    options.startBeat = 0.0;
    options.endBeat = 8.0;
    options.sampleRate = renderedWav.sampleRate; // the rate the render used
    options.bitDepth = 32;
    const auto bounce = [&](const juce::String& name) {
        const auto file = bundleDir.getSiblingFile(bundleDir.getFileName() + "_" + name + ".wav");
        const auto result = synth::BounceExporter::bounce(mc->getAudioEngine(), file, options);
        EXPECT_TRUE(result.ok) << result.message;
        EXPECT_EQ(result.streamDropouts, 0) << "the streamed clip was ready for every block";
        auto wav = readTakeWav(file);
        file.deleteFile();
        return wav;
    };
    const auto peakDifference = [](const synth::test::TakeWav& a, const synth::test::TakeWav& b) {
        float diff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < (int)std::min(a.lengthInSamples, b.lengthInSamples); ++i)
                diff = std::max(diff, std::abs(a.audio.getSample(ch, i) - b.audio.getSample(ch, i)));
        return diff > 0.0f ? 20.0 * std::log10((double)diff) : -200.0;
    };

    const auto heardAsRendered = bounce("rendered"); // the project as the command left it
    ASSERT_TRUE(heardAsRendered.ok);

    ASSERT_TRUE(doc.setTrackMuted(rendered, true));
    for (const auto& clip : doc.getTrack(doc.getTracks()[0].id)->clips)
        doc.setClipMuted(clip.id, false);
    // The first pass after the source was silenced starts its oscillator from stale state; one throwaway pass
    // settles it so the measured pass starts every note the way the render did.
    bounce("settle");
    const auto heardAsSource = bounce("source");
    ASSERT_TRUE(heardAsSource.ok);
    ASSERT_EQ(heardAsRendered.lengthInSamples, heardAsSource.lengthInSamples);

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = std::max(peak, heardAsSource.audio.getMagnitude(ch, 0, (int)heardAsSource.lengthInSamples));
    const double residualDb = peakDifference(heardAsSource, heardAsRendered);
    std::printf("[ NULL TEST ] source peak %.4f, residual peak %.2f dBFS\n", (double)peak, residualDb);
    RecordProperty("residual_dbfs", std::to_string(residualDb));
    EXPECT_GT(peak, 0.05f) << "the source is audible, so a null means something";
    EXPECT_LT(residualDb, -60.0);

    // Positive control: with both the source and the rendered track playing the two bounces differ, so the mutes
    // above really reached the audio thread and the null is not two identical renders.
    ASSERT_TRUE(doc.setTrackMuted(rendered, false));
    const auto bothPlaying = bounce("both");
    ASSERT_TRUE(bothPlaying.ok);
    EXPECT_GT(peakDifference(heardAsSource, bothPlaying), -20.0);
}
