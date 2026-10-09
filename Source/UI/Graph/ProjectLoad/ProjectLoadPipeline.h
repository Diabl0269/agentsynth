#pragma once

// ProjectLoadPipeline.h -- opening a project on screen, from the graph being built to the patch being heard
// (docs/architecture/project-bundle.md#opening-a-project-on-screen). It defers the sample and wavetable decodes,
// tracks them and the hosted plugins still instantiating, starts the canvas reveal (LoadRevealAnimator), shows what a
// slow load is waiting for, makes edits wait until it is done, and keeps the audio gate closed until the last cable
// has landed. Off screen (every headless test) an open is exactly as synchronous as it always was.

#include "Project/DeferredAssetLoads.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <set>

class AudioEngine;
class GraphEditor;

namespace synth::ui {

class EditBlockOverlay;
class LoadStageLine;

class ProjectLoadPipeline final : private juce::Timer {
public:
    /** `overlayParent` (the app window's content) holds the stage line and the edit blocks, over `editor` and, when it
     *  is inside `overlayParent`, `dock`. `status` shows a short status-bar message. `blockDetached`, when set, holds
     *  edits in the panels' detached windows (a separate top-level window no overlay over `overlayParent` reaches):
     *  called with true while edits wait and false when they are back, and the status-bar action for a refused
     *  click. All must outlive this. */
    ProjectLoadPipeline(GraphEditor& editor, AudioEngine& engine, juce::Component& overlayParent, juce::Component* dock,
                        std::function<void(const juce::String&)> status,
                        std::function<void(bool, const std::function<void()>&)> blockDetached = {});
    ~ProjectLoadPipeline() override;

    /** Message thread, first thing in a project open. On screen: ends any open still running, closes the audio gate
     *  and returns the deferral the load opens a DeferredAssetLoads::Scope on. Off screen: nullptr, nothing changes. */
    synth::DeferredAssetLoads* beginLoad();
    /** Message thread, last thing in the open (canvas built, view restored). `ok` false releases a failed load. */
    void graphBuilt(bool ok);

    /** True from beginLoad until every asset is in (on screen only). */
    bool isLoading() const noexcept { return loading_; }
    /** True (and says "Still loading") for an "Edit" command while a slow load holds edits. */
    bool refusesCommand(const juce::String& category);

    /** What the stage line says ("Loading samples 1/3"), empty when nothing is pending. */
    juce::String stageText() const;
    /** Real items in over all items, 0..1. */
    float progress() const;
    bool isStageLineShown() const;
    synth::DeferredAssetLoads& assetLoads() noexcept { return loads_; }

    /** Test seam: treat the canvas as on screen (deferral, gate, reveal stepped by hand, edit block). */
    void setForceInteractiveForTest(bool force) noexcept { forceInteractive_ = force; }
    /** Test seam: the clock the 400 ms stage-line delay is measured on. */
    void setClockForTest(std::function<double()> clockMs) { clock_ = std::move(clockMs); }
    /** Test seam: one tick of the poll timer. */
    void pollForTest() { timerCallback(); }

private:
    void timerCallback() override;
    void cancelCurrent();
    void assetInstalled(const void* owner);
    void refresh();
    void setBlocking(bool blocking);
    double nowMs() const;

    GraphEditor& editor_;
    AudioEngine& engine_;
    juce::Component& overlayParent_;
    std::function<void(const juce::String&)> status_;
    std::function<void(bool, const std::function<void()>&)> blockDetached_;
    std::function<void()> refused_;
    synth::DeferredAssetLoads loads_;
    std::unique_ptr<LoadStageLine> stageLine_;
    std::unique_ptr<EditBlockOverlay> canvasBlock_;
    std::unique_ptr<EditBlockOverlay> dockBlock_;
    std::map<const void*, uint32_t> nodeOfAsset_;
    std::set<uint32_t> loadingPlugins_;
    int pluginsTotal_ = 0;
    std::function<double()> clock_;
    double startMs_ = 0.0;
    double doneMs_ = 0.0; // when the last pending item came in
    static constexpr double kRevealWatchdogMs = 2000.0;
    bool interactive_ = false;
    bool loading_ = false;
    bool slow_ = false;
    bool forceInteractive_ = false;
};

} // namespace synth::ui
