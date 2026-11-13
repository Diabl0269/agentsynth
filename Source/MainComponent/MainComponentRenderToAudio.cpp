// Concern: Render to audio track -- which tracks render, the chunked offline render behind a progress window, moving
// the rendered files into the project like takes, and the one undo step that adds the audio tracks and mutes what they
// replace. See docs/timeline/render-to-audio.md.

#include "MainComponentRenderToAudio.h"

#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentAudioImport.h"
#include "Mixer/ChannelMacroLookup.h"
#include "Mixer/TrackChannelLink.h"
#include "Timeline/PeaksFile.h"
#include "Timeline/TakeCaptureWriter.h"
#include "Transport/StemRunner.h"
#include "UI/Chrome/RenderProgressDialog.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/PopupMotion.h"
#include <algorithm>
#include <memory>

namespace {

using AudioTake = synth::maincomponent::AudioTake;

bool progressDialogEnabled = true;

// Rendered past the range's end so a reverb or delay tail is in the file; the clip still ends at the range.
constexpr double kTailSeconds = 2.0;
constexpr int kRenderBlockSize = 512;
constexpr int kRenderChunkBlocks = 64;
constexpr int kRenderTickMs = 10;

} // namespace

// One track being rendered: where its sound comes from and what to put on the new audio track.
struct MainComponentRenderToAudio::Item {
    synth::TrackId source;
    juce::String sourceName;
    juce::uint32 colour = 0;
    double startBeat = 0.0;
    double endBeat = 0.0;
    juce::AudioProcessorGraph::NodeID strip;
    AudioTake file;             // where this item's strip render lands in the project (shared by items on one strip)
    bool fileOwner = false;     // the first item on a strip moves the file; later ones reuse its asset
    double offsetSeconds = 0.0; // the clip's start within the rendered file
};

// The whole job, shared by the runner's completion and the commit queued after it.
struct MainComponentRenderToAudio::Plan {
    std::vector<Item> items;
    double startBeat = 0.0; // the union of the items' spans: the one range the session renders
    double endBeat = 0.0;
    double secondsPerBeat = 0.5;
    juce::File scratch;
};

namespace {

using Item = MainComponentRenderToAudio::Item;
using Plan = MainComponentRenderToAudio::Plan;

bool isRenderableKind(synth::TrackKind kind) {
    return kind == synth::TrackKind::Midi || kind == synth::TrackKind::Audio;
}

} // namespace

std::vector<Item> MainComponentRenderToAudio::resolveItems(MainComponent& mc,
                                                           const std::vector<synth::ui::RenderSpan>& spans) {
    std::vector<Item> items;
    const synth::TrackChannelLinkMap links(mc.audioEngine.getGraph());
    for (const auto& span : spans) {
        const auto* track = mc.timelineDoc.getTrack(span.track);
        if (track == nullptr || !isRenderableKind(track->kind) || !(span.endBeat > span.startBeat))
            continue;
        const auto link = links.resolve(mc.timelineDoc, span.track);
        if (!link.hasChannel)
            continue;
        Item item;
        item.source = span.track;
        item.sourceName = track->name;
        item.colour = track->colourArgb;
        item.startBeat = span.startBeat;
        item.endBeat = span.endBeat;
        item.strip = link.stripId;
        items.push_back(std::move(item));
    }
    return items;
}

namespace {

void deleteRenderedFiles(const std::vector<AudioTake>& takes) {
    for (const auto& take : takes) {
        take.wavFile.deleteFile();
        take.peaksFile.deleteFile();
    }
}

} // namespace

// Moves each strip's rendered file into the project as `render-N` and writes its peaks, so the waveform draws at once.
// On any failure the files already placed are deleted and false comes back.
bool MainComponentRenderToAudio::placeRenderedFiles(MainComponent& mc, Plan& plan, const synth::StemResult& result) {
    std::vector<AudioTake> placed;
    for (auto& item : plan.items) {
        const auto found = std::find(result.stemStrips.begin(), result.stemStrips.end(), item.strip);
        if (found == result.stemStrips.end()) {
            deleteRenderedFiles(placed);
            return false;
        }
        const auto rendered = result.stemFiles[(int)(found - result.stemStrips.begin())];
        const auto owner = std::find_if(plan.items.begin(), plan.items.end(),
                                        [&item](const Item& other) { return other.strip == item.strip; });
        if (&*owner != &item) {
            item.file = owner->file; // another track on this strip already placed the file
            continue;
        }
        AudioTake take;
        if (!mc.chooseTakeFiles(take, placed, "render") || !rendered.moveFileTo(take.wavFile)) {
            deleteRenderedFiles(placed);
            return false;
        }
        placed.push_back(take);
        if (!synth::PeaksFile::writeForAudioFile(take.wavFile, take.peaksFile,
                                                 synth::TakeCaptureWriter::kPeakBucketSize)) {
            deleteRenderedFiles(placed);
            return false;
        }
        item.file = take;
    }
    return true;
}

// The edit itself, inside one recorded change: for each source a new Audio track right under it holding the render,
// and the source's clips split at the span's edges with the ones inside muted.
int MainComponentRenderToAudio::addRenderedTracks(MainComponent& mc, const Plan& plan) {
    auto& doc = mc.timelineDoc;
    int added = 0;
    for (const auto& item : plan.items) {
        // A channel several covered tracks share is rendered once, as the sum it is: the first of them gets the track.
        if (item.fileOwner && addRenderedTrack(mc, item))
            ++added;
        for (const auto inside :
             doc.splitClipsAtRangeEdges({item.source}, item.startBeat, item.endBeat, plan.secondsPerBeat))
            doc.setClipMuted(inside, true);
    }
    mc.graphEditor.updateComponents();
    return added;
}

// One new Audio track with its channel, named and coloured after `item`'s source, placed right under it and holding the
// render as one clip at the item's span.
bool MainComponentRenderToAudio::addRenderedTrack(MainComponent& mc, const Item& item) {
    auto& doc = mc.timelineDoc;
    juce::String ignoredName;
    const auto id = MainComponentAudioImport::buildAudioTrackChannel(mc, (int)doc.getTracks().size(), ignoredName);
    if (!id.isValid())
        return false;
    const juce::String name = item.sourceName + " audio";
    doc.setTrackName(id, name);
    doc.setTrackColour(id, item.colour);
    // The channel is named after its macro, which the mixer shows.
    if (const auto* track = doc.getTrack(id))
        if (const auto* channel =
                synth::nearestChannelMacro(mc.audioEngine.getGraph(), mc.graphEditor.getMacros(), track->bindingUuid))
            if (auto* macro = mc.graphEditor.getMacros().find(channel->id))
                macro->name = name;

    const auto& tracks = doc.getTracks();
    const auto source =
        std::find_if(tracks.begin(), tracks.end(), [&item](const synth::Track& t) { return t.id == item.source; });
    if (source != tracks.end())
        doc.moveTrack(id, (int)(source - tracks.begin()) + 1);

    const auto clip = doc.addClip(id, item.startBeat, item.endBeat - item.startBeat, item.sourceName);
    if (clip.isValid())
        doc.setClipAsset(clip, item.file.assetRef, item.offsetSeconds);
    return true;
}

// Queued from the render's completion rather than run inline: the edit rebuilds the clip lanes, and the completion
// still has the finished runner on the stack.
void MainComponentRenderToAudio::commitRender(Safe safe, std::shared_ptr<Plan> plan) {
    auto* mc = safe.getComponent();
    if (mc == nullptr)
        return;
    int added = 0;
    const bool pushed = [&] {
        const MainComponent::TrackGrowInScope growIn(*mc);
        return mc->undoManager.recordGraphTimelineAndMacroChange(mc->audioEngine.getGraph(), mc->timelineDoc,
                                                                 mc->graphEditor.getMacros(),
                                                                 [&] { added = addRenderedTracks(*mc, *plan); });
    }();
    mc->isBounceInProgress_ = false;
    mc->reconcileTimelineAfterGraphChange();
    if (!pushed || added == 0) {
        std::vector<AudioTake> files;
        for (const auto& item : plan->items)
            if (item.fileOwner)
                files.push_back(item.file);
        deleteRenderedFiles(files);
        mc->statusBar.showMessage("Could not add the rendered tracks");
        return;
    }
    mc->statusBar.showMessage("Rendered " + juce::String(added) + (added == 1 ? " track" : " tracks") + " to audio");
}

// The runner's completion: closes the progress window, then either cleans up (cancel, failure) or places the files and
// queues the commit.
void MainComponentRenderToAudio::finishRender(Safe safe, Window window, std::shared_ptr<Plan> plan,
                                              const synth::StemResult& result) {
    if (window != nullptr)
        synth::ui::PopupMotion::dismissModal(*window);
    auto* mc = safe.getComponent();
    if (mc == nullptr) {
        plan->scratch.deleteRecursively();
        return;
    }
    mc->stemRunner_.reset();

    const bool placed = result.ok && placeRenderedFiles(*mc, *plan, result);
    plan->scratch.deleteRecursively();
    if (!placed) {
        mc->isBounceInProgress_ = false;
        const bool cancelled = result.message.containsIgnoreCase("cancelled");
        mc->statusBar.showMessage(cancelled ? "Render cancelled" : "Render failed: " + result.message);
        return;
    }
    juce::MessageManager::callAsync([safe, plan] { commitRender(safe, plan); });
}

MainComponentRenderToAudio::Window MainComponentRenderToAudio::openProgressWindow(MainComponent& mc) {
    if (!progressDialogEnabled)
        return {};
    Safe safe(&mc);
    auto* dialog = new synth::ui::RenderProgressDialog(
        [safe] { return safe != nullptr && safe->stemRunner_ != nullptr ? safe->stemRunner_->getProgress() : 1.0; });
    dialog->onCancel = [safe] {
        if (safe != nullptr && safe->stemRunner_ != nullptr)
            safe->stemRunner_->cancel();
    };
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(dialog);
    options.dialogTitle = "Rendering to audio";
    options.componentToCentreAround = &mc;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = false;
    auto* window = synth::ui::PopupMotion::launchDialog(options);
    if (window != nullptr) {
        window->enterModalState(true, nullptr, true);
        dialog->focusCancelButton();
    }
    return window;
}

namespace {

void fillRange(Plan& plan, double secondsPerBeat) {
    plan.startBeat = plan.items.front().startBeat;
    plan.endBeat = plan.items.front().endBeat;
    for (auto& item : plan.items) {
        plan.startBeat = std::min(plan.startBeat, item.startBeat);
        plan.endBeat = std::max(plan.endBeat, item.endBeat);
    }
    plan.secondsPerBeat = secondsPerBeat;
    for (auto& item : plan.items)
        item.offsetSeconds = (item.startBeat - plan.startBeat) * secondsPerBeat;
    for (auto& item : plan.items)
        item.fileOwner = std::none_of(plan.items.begin(), plan.items.end(), [&item](const Item& other) {
            return &other < &item && other.strip == item.strip;
        });
}

} // namespace

std::vector<synth::TrackId> MainComponentRenderToAudio::renderableTracks(MainComponent& mc,
                                                                         const std::vector<synth::TrackId>& tracks) {
    std::vector<synth::ui::RenderSpan> spans;
    for (const auto track : tracks)
        spans.push_back({track, 0.0, 1.0});
    std::vector<synth::TrackId> renderable;
    for (const auto& item : resolveItems(mc, spans))
        renderable.push_back(item.source);
    return renderable;
}

bool MainComponentRenderToAudio::request(MainComponent& mc, const std::vector<synth::ui::RenderSpan>& spans) {
    if (mc.isRecordingActive()) {
        mc.statusBar.showMessage("Can't render to audio while recording");
        return false;
    }
    if (mc.isBounceInProgress_) {
        mc.statusBar.showMessage("Can't render to audio while an export or another render is running");
        return false;
    }
    auto plan = std::make_shared<Plan>();
    plan->items = resolveItems(mc, spans);
    if (plan->items.empty()) {
        mc.statusBar.showMessage("Nothing to render: no track in the selection has a mixer channel");
        return false;
    }

    const auto position = mc.audioEngine.getTransport().getPositionSnapshot();
    fillRange(*plan, 60.0 / (position.bpm > 0.0 ? position.bpm : 120.0));

    synth::BounceOptions options;
    options.startBeat = plan->startBeat;
    options.endBeat = plan->endBeat;
    options.tailSeconds = kTailSeconds;
    const double rate = mc.audioEngine.getGraph().getSampleRate();
    options.sampleRate = rate > 0.0 ? rate : 44100.0;
    options.blockSize = kRenderBlockSize;
    options.bitDepth = 24;
    options.numChannels = 2;
    options.format = synth::BounceFormat::Wav;

    synth::StemSessionOptions session;
    session.tapBeforeSolo = true;
    for (const auto& item : plan->items) {
        session.onlyStrips.push_back(item.strip);
        session.unmuteStrips.push_back(item.strip);
    }

    plan->scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getNonexistentChildFile("agentsynth-render", "", false);

    // Publishing right before the render, as export does, makes a stale timeline binding impossible.
    mc.publishTimelineAndRebindRecorder();
    mc.isBounceInProgress_ = true;

    Safe safe(&mc);
    auto window = std::make_shared<Window>();
    mc.stemRunner_ = std::make_unique<synth::StemRunner>(
        mc.audioEngine, plan->scratch, options,
        [safe, window, plan](synth::StemResult result) { finishRender(safe, *window, plan, result); },
        kRenderChunkBlocks, kRenderTickMs, nullptr, session);
    *window = openProgressWindow(mc);
    return true;
}

void MainComponentRenderToAudio::setProgressDialogEnabledForTest(bool enabled) { progressDialogEnabled = enabled; }

bool MainComponentRenderToAudio::isIdleForTest(const MainComponent& mc) { return mc.isExportAvailable(); }

void MainComponentRenderToAudio::cancelForTest(MainComponent& mc) {
    if (mc.stemRunner_ != nullptr)
        mc.stemRunner_->cancel();
}
