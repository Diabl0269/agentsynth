// ProjectLoadPipeline.cpp -- the open's stages: begin (gate closed, decodes deferred), graph built (reveal started,
// pending items counted), each item landing, done (edits back, stage line gone), and the gate opening once the reveal
// has connected the last cable.

#include "ProjectLoadPipeline.h"

#include "AudioEngine/AudioEngine.h"
#include "EditBlockOverlay.h"
#include "LoadRevealAnimator.h"
#include "LoadStageLine.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

namespace lr = load_reveal;

ProjectLoadPipeline::ProjectLoadPipeline(GraphEditor& editor, AudioEngine& engine, juce::Component& overlayParent,
                                         juce::Component* dock, std::function<void(const juce::String&)> status,
                                         std::function<void(bool, const std::function<void()>&)> blockDetached)
    : editor_(editor)
    , engine_(engine)
    , overlayParent_(overlayParent)
    , status_(std::move(status))
    , blockDetached_(std::move(blockDetached)) {
    refused_ = [this] {
        if (status_)
            status_("Still loading");
    };
    const auto& refused = refused_;
    canvasBlock_ = std::make_unique<EditBlockOverlay>(editor_, refused);
    overlayParent_.addChildComponent(*canvasBlock_);
    if (dock != nullptr && overlayParent_.isParentOf(dock)) {
        dockBlock_ = std::make_unique<EditBlockOverlay>(*dock, refused);
        overlayParent_.addChildComponent(*dockBlock_);
    }
    stageLine_ = std::make_unique<LoadStageLine>(editor_);
    overlayParent_.addChildComponent(*stageLine_);
    loads_.onInstalled = [this](const void* owner) { assetInstalled(owner); };
}

ProjectLoadPipeline::~ProjectLoadPipeline() {
    stopTimer();
    if (blockDetached_)
        blockDetached_(false, {});
    loads_.onInstalled = nullptr;
    for (auto* overlay :
         {static_cast<juce::Component*>(canvasBlock_.get()), static_cast<juce::Component*>(dockBlock_.get()),
          static_cast<juce::Component*>(stageLine_.get())})
        if (overlay != nullptr)
            overlayParent_.removeChildComponent(overlay);
}

double ProjectLoadPipeline::nowMs() const { return clock_ ? clock_() : juce::Time::getMillisecondCounterHiRes(); }

// Whether the open is interactive is decided here, once: on screen, or forced by a test. Anything else (a headless
// open, a launch before the window shows) keeps the synchronous behaviour every existing caller relies on.
synth::DeferredAssetLoads* ProjectLoadPipeline::beginLoad() {
    cancelCurrent();
    interactive_ = forceInteractive_ || editor_.isShowing();
    if (!interactive_)
        return nullptr;
    loading_ = true;
    slow_ = false;
    startMs_ = nowMs();
    engine_.setLoadGateOpen(false);
    return &loads_;
}

void ProjectLoadPipeline::cancelCurrent() {
    stopTimer();
    editor_.getLoadReveal().finish();
    loads_.reset();
    nodeOfAsset_.clear();
    loadingPlugins_.clear();
    pluginsTotal_ = 0;
    loading_ = false;
    slow_ = false;
    setBlocking(false);
    stageLine_->update({}, 0.0f, false);
    engine_.setLoadGateOpen(true);
}

// The pending items are the decodes still in flight and every hosted plugin still instantiating, each on the node
// whose card waits for it. Any pending item makes the load slow: outlines, edits held, and after 400 ms the stage line.
void ProjectLoadPipeline::graphBuilt(bool ok) {
    if (!interactive_)
        return;
    if (!ok) {
        cancelCurrent();
        return;
    }
    std::map<const void*, uint32_t> nodeOfProcessor;
    std::set<uint32_t> pending;
    for (auto* node : engine_.getGraph().getNodes()) {
        nodeOfProcessor[node->getProcessor()] = node->nodeID.uid;
        if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor());
            hosted && hosted->isLoading())
            loadingPlugins_.insert(node->nodeID.uid);
    }
    pluginsTotal_ = (int)loadingPlugins_.size();
    pending = loadingPlugins_;
    for (const void* owner : loads_.pendingOwners())
        if (const auto it = nodeOfProcessor.find(owner); it != nodeOfProcessor.end()) {
            nodeOfAsset_[owner] = it->second;
            pending.insert(it->second);
        }
    slow_ = !pending.empty();
    doneMs_ = nowMs();

    const auto motion = animationsOff()          ? lr::Motion::off
                        : prefersReducedMotion() ? lr::Motion::reduced
                                                 : lr::Motion::full;
    auto& reveal = editor_.getLoadReveal();
    reveal.onLanded = [this] { refresh(); };
    reveal.start(motion, pending, editor_.isShowing());
    setBlocking(slow_);
    refresh();
    if (loading_ || !engine_.isLoadGateOpen())
        startTimerHz(30);
}

void ProjectLoadPipeline::assetInstalled(const void* owner) {
    if (const auto it = nodeOfAsset_.find(owner); it != nodeOfAsset_.end()) {
        for (auto* card : editor_.getModuleComponents())
            if (card != nullptr && card->getNodeId().uid == it->second)
                card->refreshLoadedFileLabels();
        editor_.getLoadReveal().setNodeReady(it->second);
    }
    refresh();
}

// The poll runs only from graphBuilt until the gate opens: it notices plugins finishing (or their node going) and
// shows the stage line once the load has taken 400 ms.
void ProjectLoadPipeline::timerCallback() {
    for (auto it = loadingPlugins_.begin(); it != loadingPlugins_.end();) {
        auto* node = engine_.getGraph().getNodeForId(juce::AudioProcessorGraph::NodeID(*it));
        auto* hosted = node != nullptr ? dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()) : nullptr;
        if (hosted != nullptr && hosted->isLoading()) {
            ++it;
            continue;
        }
        const uint32_t uid = *it;
        it = loadingPlugins_.erase(it);
        editor_.getLoadReveal().setNodeReady(uid);
    }
    refresh();
}

// Done loading when nothing is pending: edits come back and the stage line leaves. The gate opens once the reveal has
// landed too (or was ended some other way), then the poll stops.
//
// A reveal still running long after everything is in (no VBlank reaches a hidden or minimised window) is landed, so
// the patch is never left silent behind an animation nobody can see.
void ProjectLoadPipeline::refresh() {
    if (loading_ && !loads_.hasPending() && loadingPlugins_.empty()) {
        loading_ = false;
        doneMs_ = nowMs();
        setBlocking(false);
    }
    if (!loading_ && editor_.getLoadReveal().isLive() && nowMs() - doneMs_ > kRevealWatchdogMs)
        editor_.getLoadReveal().finish();
    const bool shown = loading_ && slow_ && nowMs() - startMs_ >= lr::kStageLineDelayMs;
    stageLine_->update(stageText(), progress(), shown);
    if (!loading_ && !editor_.getLoadReveal().isLive()) {
        engine_.setLoadGateOpen(true);
        stopTimer();
    }
}

void ProjectLoadPipeline::setBlocking(bool blocking) {
    editor_.getLoadReveal().setBlockingEdits(blocking);
    canvasBlock_->setBlocking(blocking);
    if (dockBlock_ != nullptr)
        dockBlock_->setBlocking(blocking);
    if (blockDetached_)
        blockDetached_(blocking, refused_);
}

bool ProjectLoadPipeline::refusesCommand(const juce::String& category) {
    return category == "Edit" && editor_.getLoadReveal().refuseEdit();
}

juce::String ProjectLoadPipeline::stageText() const {
    const auto line = [](const char* what, int done, int total) {
        return juce::String("Loading ") + what + " " + juce::String(done) + "/" + juce::String(total);
    };
    const auto samples = loads_.counts(DeferredAssetLoads::Kind::Sample);
    if (samples.done < samples.total)
        return line("samples", samples.done, samples.total);
    const auto tables = loads_.counts(DeferredAssetLoads::Kind::Wavetable);
    if (tables.done < tables.total)
        return line("wavetables", tables.done, tables.total);
    const int pluginsDone = pluginsTotal_ - (int)loadingPlugins_.size();
    if (pluginsDone < pluginsTotal_)
        return line("plugins", pluginsDone, pluginsTotal_);
    return {};
}

float ProjectLoadPipeline::progress() const {
    const auto samples = loads_.counts(DeferredAssetLoads::Kind::Sample);
    const auto tables = loads_.counts(DeferredAssetLoads::Kind::Wavetable);
    const int total = samples.total + tables.total + pluginsTotal_;
    const int done = samples.done + tables.done + pluginsTotal_ - (int)loadingPlugins_.size();
    return total > 0 ? (float)done / (float)total : 1.0f;
}

bool ProjectLoadPipeline::isStageLineShown() const { return stageLine_->isShown(); }

} // namespace synth::ui
