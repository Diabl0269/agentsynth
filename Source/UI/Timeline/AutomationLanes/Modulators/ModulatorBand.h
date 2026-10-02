#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorInfo.h"
#include "UI/Timeline/EditTool.h"
#include "UI/Timeline/TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class AppUndoManager; // Forward declaration (Source/AppUndoManager.h)

namespace synth {
class TransportService;
}

namespace synth::ui {

struct TrackHeaderHost;

// The lanes-region half of a modulator row: the routing's AMOUNT over the song (ModulatorAmountLane.h).
// A routing through a hidden Attenuverter gets an editable band; a direct cable has no amount and its band
// is a plain decoration that takes no clicks, so the clip lanes underneath still decide.
//
// The band draws and edits through an ordinary AutomationLaneEditor pointed at a private one-lane proxy doc,
// never at the real one: the proxy mirrors the real amount lane (or, with none, is an empty lane whose
// default is the Attenuverter's current amount, so the editor paints that flat line), and every edit the
// editor makes to the proxy is written into the real doc as ONE undo step that also creates the lane on
// the first stroke and removes it with the last point. While no amount lane exists, the Select tool's
// vertical drag and Up/Down set the Attenuverter's knob value instead (one graph undo step per gesture).
// Message thread only.
class ModulatorBand
    : public juce::Component
    , public juce::TooltipClient
    , private synth::TimelineDoc::Listener
    , private juce::AsyncUpdater {
public:
    static constexpr float kBandAlpha = 0.28f;
    static constexpr double kNudgeStep = 0.01;      // Up/Down
    static constexpr double kLargeNudgeStep = 0.10; // Shift+Up/Down

    explicit ModulatorBand(TimelineViewState& viewState);
    ~ModulatorBand() override;

    // Non-owning, null-safe, like every other timeline sub-component's.
    void setTimelineDoc(synth::TimelineDoc* doc) noexcept { doc_ = doc; }
    void setUndoManager(AppUndoManager* undo) noexcept { undo_ = undo; }
    void setTransport(synth::TransportService* transport);
    void setHost(TrackHeaderHost* host) noexcept { host_ = host; }
    void setEditTool(EditTool tool);
    /** The Draw tool's shape, so a box stamp works on the amount lane like on any lane. */
    void setDrawShape(DrawShape shape);

    /** The routing this band stands for, under `ownerLane` (whose track a new amount lane goes on). */
    void setModulator(const ModulatorInfo& info, synth::LaneId ownerLane, const juce::String& parameterName);
    /** Re-reads the doc: the proxy lane, the names, the picture. Call on every doc change. */
    void refreshFromDoc();
    /** The owning track's colour; the curve is drawn in it, pushed to a readable contrast. */
    void setTrackColour(juce::Colour colour);
    /** The amount the row shows (the lane at the playhead, else the knob); re-syncs the flat line. */
    void setAmountReadout(double amount);

    /** True for a routing with an Attenuverter; a direct cable's band is a plain decoration. */
    bool isEditable() const noexcept { return info_.attenuverterUuid.isNotEmpty(); }
    const synth::AutomationLane* amountLane() const;
    /** The curve editor over the proxy doc; nullptr for a decoration band. */
    AutomationLaneEditor* getEditor() noexcept { return editor_.get(); }
    /** The proxy doc the editor edits (test seam). */
    const synth::TimelineDoc& getProxyDoc() const noexcept { return proxy_; }

    juce::String getTooltip() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** The amount the screen reader and the readout use now. */
    double currentAmount() const noexcept { return readout_; }
    /** Sets the Attenuverter's knob value as one complete edit (keys, a screen reader). No-op with a lane. */
    void setKnobAmount(double amount);

private:
    void applyNames();
    void updateClickRouting();
    synth::TrackId ownerTrack() const;
    double knobAmount() const;

    // The proxy doc (ModulatorBandEdits.cpp)
    void timelineChanged(const synth::TimelineDoc& doc) override;
    void handleAsyncUpdate() override;
    void syncProxy();
    void rebuildProxyLane(float defaultValue);
    void commitProxy();
    void createLaneWithPoint(double beat, double value);
    void writeToRealDoc(const std::vector<synth::AutomationLane::Breakpoint>& points);

    TimelineViewState& viewState_;
    synth::TimelineDoc* doc_ = nullptr;
    AppUndoManager* undo_ = nullptr;
    synth::TransportService* transport_ = nullptr;
    TrackHeaderHost* host_ = nullptr;
    EditTool tool_ = EditTool::Select;
    DrawShape drawShape_ = DrawShape::Free;

    ModulatorInfo info_;
    synth::LaneId ownerLane_;
    juce::String parameterName_;
    double readout_ = 0.0;

    synth::TimelineDoc proxy_;
    synth::TrackId proxyTrack_;
    synth::LaneId proxyLane_;
    std::unique_ptr<AutomationLaneEditor> editor_; // after proxy_: destroyed first, it points into it
    bool syncingProxy_ = false;                    // the band itself is writing the proxy: not an edit to commit
    bool committing_ = false; // a write into the real doc is in flight: the doc is between its mutations

    // The knob drag (Select tool, no amount lane)
    bool knobDragging_ = false;
    double knobDragStart_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulatorBand)
};

} // namespace synth::ui
