#pragma once

// The panel a plain click on a jack opens: the cables on that jack, one row each (PortConnectionRow), a "Disconnect
// all" button under two or more, and the title that counts them. It is the content of a ModDotPanelFrame window, built
// from the mod dot panel's parts (palette, rows, remove glyph, motion). Edits go straight to the graph through
// GraphEditor (one undo step each); the list follows the graph while open. A removed row shrinks toward its centre and
// the rows below close the gap; an undo makes room, grows the row back and fades an outline around it
// (ExitEnterRowSlots.h); the panel's height follows frame by frame. Removing the last connection keeps the panel open
// showing "No connections yet". Esc closes it. docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include "PortConnectionRow.h"
#include "UI/Graph/ModDot/ModDotAddSourceParts.h"
#include "UI/Graph/ModDot/ModDotPage.h"
#include "UI/Layout/ExitEnterRowSlots.h"
#include <memory>
#include <vector>

namespace synth::ui {

class PortPanelController;

class PortConnectionsPanel final
    : public ModDotPage
    , private juce::Timer {
public:
    static constexpr int kTitleHeight = 30;
    static constexpr int kFooterHeight = 32;
    static constexpr int kBottomPad = 6;
    static constexpr int kScrollBar = 10;
    // PHASE 2 ("Add connection" row, fed by the same ports the cable drag can reach) goes between the rows and the
    // footer, in arrange(); it needs no change to the rows above it.

    PortConnectionsPanel(GraphEditor& editor, PortPanelController& controller, PortRef port);
    ~PortConnectionsPanel() override;

    const PortRef& port() const noexcept { return port_; }
    int preferredHeight() const override { return desiredHeight_; }
    void focusEntry() override;

    /** Re-reads the jack's cables: new ones grow in, gone ones shrink out. Dismisses the panel when the node is gone.
     */
    void sync();
    /** The tallest the panel may be (the room on screen); the rows scroll inside what is left. 0 = no limit. */
    void setMaxHeight(int height);
    /** The controller is going away: close without telling it. */
    void orphan() { controller_ = nullptr; }
    /** Closes the window the panel sits in (with `onDismiss` set, tells the owner to). */
    void dismiss();
    std::function<void()> onDismiss;

    // Test seams and inspection.
    int rowCount() const; // rows not shrinking out
    PortConnectionRow* rowAt(int index) const;
    PortConnectionRow* rowFor(const GraphEditor::CableId& id) const;
    juce::String titleText() const;
    juce::Button& disconnectAllButton() noexcept { return disconnectAll_; }
    bool isDisconnectAllShown() const noexcept { return footer_.to > 0.0f; }
    bool isAnimating() const noexcept { return anim_.isRunning(); }
    /** Test seam: animate even though the panel is not showing. */
    void setForceAnimateForTest(bool on) noexcept { forceAnimate_ = on; }
    /** Test seam: puts a removal or restore motion at `elapsedMs` after it began (what each frame does). */
    void applyTimelineAtMs(double elapsedMs);
    const ExitEnterTimeline& timeline() const noexcept { return timeline_; }
    int slotHeightFor(const GraphEditor::CableId& id) const;
    float outlineAlphaFor(const GraphEditor::CableId& id) const;
    /** The row for `id` including one shrinking away (rowFor skips it); null when there is none. */
    PortConnectionRow* animatingRowFor(const GraphEditor::CableId& id) const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

private:
    struct Entry : ExitEnterSlot {
        std::unique_ptr<PortConnectionRow> row;
    };
    struct Footer {
        float from = 0.0f;
        float to = 0.0f;
        float current = 0.0f;
    };
    class RowsHolder;

    void wireRow(PortConnectionRow& row);
    void removeConnection(PortConnectionRow& row);
    void disconnectEverything();
    bool followsSameConnections(const std::vector<PortConnection>& list) const;
    void animateLayout();
    void startRemovalMotion();
    void landMotion();
    void layoutRows();
    void arrange();
    void finishLeaving();
    void navigate(juce::Component* from, int step);
    void scrollRowIntoView(const PortConnectionRow& row);
    void timerCallback() override;
    bool focusIsInside() const;
    void ensureFocusInside();

    GraphEditor& editor_;
    PortPanelController* controller_;
    PortRef port_;
    juce::String ownTitle_;
    std::vector<Entry> entries_;
    std::unique_ptr<RowsHolder> holder_;
    juce::Viewport viewport_;
    ModDotLinkButton disconnectAll_;
    Footer footer_;
    int maxHeight_ = 0;
    int rowsTotal_ = 0;
    int desiredHeight_ = kTitleHeight + kBottomPad;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver anim_;
    ExitEnterTimeline timeline_;
    bool motionActive_ = false;
    bool forceAnimate_ = false;
    bool reducedMotion_ = false;
    int focusTries_ = 0;
};

} // namespace synth::ui
