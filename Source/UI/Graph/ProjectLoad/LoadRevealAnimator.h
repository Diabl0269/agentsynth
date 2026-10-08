#pragma once

// LoadRevealAnimator.h -- a project opening on screen comes to life (docs/layout/animation.md#project-load-reveal):
// every card is hidden, then pops in along the signal flow, and each cable draws out once both its ends are there.
// A card still waiting for its assets shows a faint outline and pops when they land. Geometry is final throughout:
// only each card's alpha and a temporary component transform change, and cables are shortened at paint time.
// GraphEditor owns it (getLoadReveal) and lends it the canvas through Hooks; ProjectLoadPipeline drives it.

#include "LoadRevealTimeline.h"
#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace synth {
class MacroSet;
}

class LoadRevealAnimator {
public:
    /** One visible card: `nodeUid` for a module card, `macroId` for a collapsed macro card. */
    struct Card {
        juce::Component* comp = nullptr;
        uint32_t nodeUid = 0;
        juce::String macroId;
    };

    /** What the canvas lends. All are called on the message thread. */
    struct Hooks {
        std::function<std::vector<Card>()> cards;
        std::function<juce::AudioProcessorGraph&()> graph;
        std::function<const synth::MacroSet&()> macros;
        std::function<bool(uint32_t nodeUid)> isOutputNode;
        std::function<const std::vector<graph_editor_types::VisibleCable>&()> cables;
        /** The painted border of an open macro, canvas coordinates. */
        std::function<juce::Rectangle<int>(const juce::String& macroId)> hullBounds;
        std::function<void(juce::Rectangle<int>)> repaintArea;
        std::function<void()> repaintAll;
        std::function<void(float)> setCanvasAlpha;
        /** zoom x display scale: what a card's raster is pinned at while it pops. */
        std::function<float()> rasterScale;
        std::function<juce::Colour()> outlineColour;
        std::function<void(const juce::String&)> reportStatus;
        juce::VBlankAnimatorUpdater* updater = nullptr;
    };

    LoadRevealAnimator() = default;
    ~LoadRevealAnimator();
    LoadRevealAnimator(const LoadRevealAnimator&) = delete;
    LoadRevealAnimator& operator=(const LoadRevealAnimator&) = delete;

    /** Message thread. Set once, before start(). */
    void setHooks(Hooks hooks);

    /** Hides every card and starts the wave. `pendingNodes` are still loading assets: their groups stay outlined until
     *  setNodeReady. `drive` false: no VBlank reaches the canvas, frames come only from applyAtMs (tests). */
    void start(synth::ui::load_reveal::Motion motion, const std::set<uint32_t>& pendingNodes, bool drive);
    /** A pending node's assets are in; its group pops once all of its pending nodes are. */
    void setNodeReady(uint32_t nodeUid);
    /** Lands everything at once and restores every card. Safe at any time, also from inside a frame. */
    void finish();
    bool isLive() const noexcept { return live_; }
    /** Fires once every group has appeared and the last cable has landed (not on finish()). */
    std::function<void()> onLanded;

    // ---- Paint queries (GraphContentComponent::paint / paintOverChildren) ----
    /** How much of a cable from `srcUid` to `dstUid` is drawn, 0..1; 1 when nothing is revealing. */
    float cableProgress(uint32_t srcUid, uint32_t dstUid) const noexcept;
    /** Opacity of an open macro's border, 0..1; 1 when nothing is revealing. */
    float hullAlpha(const juce::String& macroId) const noexcept;
    /** Paints everything in its scope at `alpha` (a transparency layer only when alpha < 1). */
    class ScopedFade {
    public:
        ScopedFade(juce::Graphics& g, float alpha)
            : g_(g)
            , layered_(alpha < 1.0f) {
            if (layered_)
                g_.beginTransparencyLayer(alpha);
        }
        ~ScopedFade() {
            if (layered_)
                g_.endTransparencyLayer();
        }
        ScopedFade(const ScopedFade&) = delete;
        ScopedFade& operator=(const ScopedFade&) = delete;

    private:
        juce::Graphics& g_;
        bool layered_;
    };
    /** The faint outlines of cards still waiting for their assets (slow load only). */
    void paintOutlines(juce::Graphics& g) const;
    /** True while a frame sets cards' transforms: the canvas must not treat that as a card moving. */
    bool isApplyingFrame() const noexcept { return applying_; }

    // ---- Edits wait while a slow load runs ----
    void setBlockingEdits(bool blocking) noexcept { blockingEdits_ = blocking; }
    bool isBlockingEdits() const noexcept { return blockingEdits_; }
    /** True (and says "Still loading") while edits are blocked: the caller drops the edit. */
    bool refuseEdit();

    // ---- Test seams ----
    /** One frame at `elapsedMs` since start(), exactly as the VBlank runs it. */
    void applyAtMs(double elapsedMs);
    double elapsedMs() const noexcept { return nowMs_; }
    /** When the reveal lands, as scheduled now; huge while a group is pending. */
    double endMs() const noexcept;
    const synth::ui::load_reveal::Timeline& timeline() const noexcept { return timeline_; }
    int groupOfNode(uint32_t nodeUid) const noexcept;
    int groupOfCard(const juce::Component* card) const noexcept;
    int outlineCount() const noexcept;

private:
    struct Group {
        std::vector<juce::Component::SafePointer<juce::Component>> cards;
        std::vector<juce::String> hulls; // open macros whose border fades with this group
        int pendingNodes = 0;
        bool hasOutputNode = false;
        float applied = -1.0f; // the pop progress last applied to its cards
    };

    void buildGroups(const std::set<uint32_t>& pendingNodes);
    void applyGroup(Group& group, float progress);
    juce::Rectangle<int> groupArea(const Group& group) const;
    juce::Rectangle<int> cableArea(double fromMs, double toMs) const;
    void startDriver();
    void frameAt(double elapsedMs);
    double clockMs() const;

    Hooks hooks_;
    std::vector<Group> groups_;
    std::unordered_map<uint32_t, int> groupOfNode_;
    std::map<juce::String, int> groupOfHull_;
    std::vector<std::pair<int, int>> cableGroups_;
    std::set<uint32_t> pendingNodes_;
    synth::ui::load_reveal::Timeline timeline_;
    synth::ui::load_reveal::Motion motion_ = synth::ui::load_reveal::Motion::full;
    synth::ui::AnimationDriver driver_;
    double startClockMs_ = 0.0;
    double nowMs_ = 0.0;
    bool live_ = false;
    bool drive_ = false;
    bool outlines_ = false;
    bool inFrame_ = false;
    bool applying_ = false;
    bool blockingEdits_ = false;
};
