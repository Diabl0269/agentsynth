#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineViewState.h"
#include "UI/Timeline/TrackAutomationLanes/LaneGlyphs.h"
#include "UI/Timeline/TrackAutomationLanes/LaneToolMapping.h"
#include "UI/Timeline/TrackAutomationLanes/TrackLaneHeaderComponent.h"
#include "UI/Timeline/TrackRowLayout.h"
#include <functional>
#include <memory>
#include <vector>

class AppUndoManager; // Source/AppUndoManager.h

namespace synth {
class TransportService;
}

// TrackAutomationLanes -- TimelinePanelComponent's collaborator for per-track automation lane rows
// (docs/timeline/track-automation.md): which tracks are expanded, the pooled lane-row headers (in
// the track-header column) and lane-row curve editors (in a click-through layer over the clip
// lanes), which lane row is focused, the lane editors' tool, and the toolbar's automation controls
// (Draw-tool curve selector, global-strip toggle, automation-follows-clips toggle).
//
// Owns no geometry: every y comes from the TrackRowLayout the clip-lane area builds, handed in by
// the panel (layoutRows / layoutEditors). Message thread only; every pointer setter is nullable.
namespace synth::ui {

struct TrackHeaderHost;

class TrackAutomationLanes {
public:
    /** Panel hooks; each may be left unset. */
    struct Callbacks {
        std::function<void()> relayout;                                // re-run the row layout
        std::function<const TrackRowLayout*()> rowLayout;              // the current layout
        std::function<void(int top, int bottom)> ensureContentVisible; // content px
        std::function<void()> toggleGlobalStrip;                       // global-automation button
        std::function<void()> toggleFollowsClips;                      // follows-clips button
    };

    /** `headerList` parents the lane headers; the editor layer must be added by the owner. */
    TrackAutomationLanes(TimelineViewState& viewState, juce::Component& headerList);
    ~TrackAutomationLanes();

    void setCallbacks(Callbacks callbacks) { callbacks_ = std::move(callbacks); }
    void setTimelineDoc(synth::TimelineDoc* doc);
    void setUndoManager(AppUndoManager* undoManager);
    void setTrackHeaderHost(TrackHeaderHost* host);
    void setTransport(synth::TransportService* transport);

    // ---- Expansion (TimelineViewState::expandedLaneTracks) ----
    bool isExpanded(synth::TrackId track) const;
    /** Fires Callbacks::relayout when the state changes. */
    void setExpanded(synth::TrackId track, bool expanded);

    // ---- Reveal / focus ----
    /** Expands, scrolls to and focuses a TRACK-owned lane; false (nothing done) for a global lane. */
    bool revealLane(synth::LaneId lane);
    void focusLane(synth::LaneId lane);
    synth::LaneId getFocusedLane() const noexcept { return focusedLane_; }

    // ---- Doc / layout ----
    void refreshFromDoc();
    /** Positions lane headers (content coords, `headerWidth` wide) and the visible editors. */
    void layoutRows(const TrackRowLayout& layout, int headerWidth);
    /** Re-positions the editors only (after a scroll); creates/reuses them for visible rows only. */
    void layoutEditors(const TrackRowLayout& layout);
    /** The click-through layer the editors live in; the owner sizes it over the clip lanes. */
    juce::Component& getEditorLayer() noexcept { return editorLayer_; }

    // ---- Tools ----
    void setEditTool(EditTool tool);
    void setCurve(LaneCurve curve);
    LaneCurve getCurve() const noexcept { return curve_; }
    /** The curve popup's headless hook: menu id = 1 + LaneCurve index. */
    void applyCurveMenuChoice(int menuId);

    // ---- Toolbar ----
    /** Adds the three toolbar buttons as children of `parent`. Call once. */
    void addToolbarTo(juce::Component& parent);
    /** Lays the buttons out, taking their width off the RIGHT of `area`. */
    void layoutToolbar(juce::Rectangle<int>& area);
    void refreshToolbar(int globalLaneCount, bool stripOpen, bool followsClips);
    LaneGlyphButton& getCurveButton() noexcept { return curveButton_; }
    LaneGlyphButton& getGlobalAutomationButton() noexcept { return globalButton_; }
    LaneGlyphButton& getFollowsClipsButton() noexcept { return followsButton_; }

    // ---- Test accessors ----
    AutomationLaneEditor* getEditorForLane(synth::LaneId lane) const;
    TrackLaneHeaderComponent* getLaneHeaderForLane(synth::LaneId lane) const;
    int getVisibleEditorCount() const;
    int getVisibleLaneHeaderCount() const;

private:
    struct FocusListener : juce::MouseListener {
        explicit FocusListener(TrackAutomationLanes& owner)
            : owner_(owner) {}
        void mouseDown(const juce::MouseEvent& e) override;
        TrackAutomationLanes& owner_;
    };

    AutomationLaneEditor& editorFor(synth::LaneId lane, std::vector<bool>& claimed);
    TrackLaneHeaderComponent& headerFor(std::size_t index);
    void applyToolToEditor(AutomationLaneEditor& editor) const;
    juce::Colour trackColourForLane(synth::LaneId lane) const;
    void showCurveMenu();
    void setUpToolbarButtons();
    void applyToolToAllEditors();

    TimelineViewState& viewState_;
    juce::Component& headerList_;
    Callbacks callbacks_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undoManager_ = nullptr;
    TrackHeaderHost* host_ = nullptr;
    synth::TransportService* transport_ = nullptr;

    juce::Component editorLayer_;
    std::vector<std::unique_ptr<AutomationLaneEditor>> editors_;     // pooled, never freed mid-session
    std::vector<std::unique_ptr<TrackLaneHeaderComponent>> headers_; // pooled, never freed mid-session
    FocusListener focusListener_{*this};
    synth::LaneId focusedLane_;

    EditTool editTool_ = EditTool::Select;
    LaneCurve curve_ = LaneCurve::Freehand;
    LaneGlyphButton curveButton_;
    LaneGlyphButton globalButton_;
    LaneGlyphButton followsButton_;
};

} // namespace synth::ui
