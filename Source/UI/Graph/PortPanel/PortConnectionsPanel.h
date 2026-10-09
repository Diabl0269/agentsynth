#pragma once

// The panel a plain click on a jack opens: the cables on that jack, one row each (PortConnectionRow), an "Add
// connection | Pick on canvas" split button under them, a "Disconnect all" button under two or more, and the title
// that counts them. It is the content of a ModDotPanelFrame window, built from the mod dot panel's parts (palette,
// rows, remove glyph, motion). Edits go straight to the graph through GraphEditor (one undo step each); the list
// follows the graph while open. A removed row shrinks toward its centre and the rows below close the gap; an undo
// makes room, grows the row back and fades an outline around it (ExitEnterRowSlots.h); the panel's height follows
// frame by frame. Removing the last connection keeps the panel open showing "No connections yet".
//
// "Add connection" swaps the list for the search page (PortTargetSearchPage) in the SAME panel: the two pages
// cross-fade while the height settles to the new page's (160 ms ease-out going in, 110 ms ease-in coming back; under
// Reduce Motion an 80 ms fade with the height changing at once). A pick connects, goes back to the list and the new
// row grows in. "Pick on canvas" (ModDotCanvasPicker's jack mode) connects to the jack or knob clicked on the canvas.
// Esc steps back: stop picking, clear the search, return to the list, then close.
// docs/layout/cables.md#port-connections-panel.

#include "PortConnectionList.h"
#include "PortConnectionRow.h"
#include "PortTargetSearchPage.h"
#include "UI/Graph/ModDot/ModDotAddSourceParts.h"
#include "UI/Graph/ModDot/ModDotCanvasPicker.h"
#include "UI/Graph/ModDot/ModDotPage.h"
#include "UI/Layout/ExitEnterRowSlots.h"
#include "UI/Layout/SplitButton.h"
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
    static constexpr int kSplitRowHeight = 32; // the Add connection | Pick on canvas row
    static constexpr double kPageInMs = 160.0; // list -> search page
    static constexpr double kPageOutMs = 110.0;
    static constexpr double kPageReducedMs = 80.0; // Reduce Motion: a plain fade

    PortConnectionsPanel(GraphEditor& editor, PortPanelController& controller, PortRef port);
    ~PortConnectionsPanel() override;

    const PortRef& port() const noexcept { return port_; }
    int preferredHeight() const override;
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

    // ---- Add connection ----
    /** Swaps to the search page (focus in its field) or back to the list, cross-fading and settling the height. */
    void setSearchOpen(bool open);
    bool isSearchOpen() const noexcept { return searchOpen_; }
    /** "Pick on canvas": the next press on a jack or knob the jack can connect to connects it; Esc stops. */
    void startPick();
    void stopPick();
    void togglePick();
    bool isPicking() const noexcept { return picker_ != nullptr; }

    // Test seams and inspection.
    PortTargetSearchPage& searchPage() noexcept { return *searchPage_; }
    ModDotCanvasPicker* canvasPicker() noexcept { return picker_.get(); }
    SplitButton& splitButton() noexcept { return split_; }
    juce::Button& addConnectionButton() noexcept { return split_.leftHalf(); }
    /** 0 = the list is showing, 1 = the search page; between them the two are cross-fading. */
    float searchAmount() const noexcept { return searchAmount_; }
    /** The height the panel settles at with the search page open or closed. */
    int settledHeight(bool searchOpen) const;
    /** Puts the page swap at eased progress `t` (what each frame does). */
    void applyPageTweenAt(float t);
    bool isPageAnimating() const noexcept { return pageAnim_.isRunning(); }
    /** Re-reads the targets now (the tick does it a few times a second). */
    void refreshTargets();
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

    void connectTarget(const PortTarget& target);
    const PortTarget* pickableTargetFor(const ModDotCanvasPicker::JackHit& hit) const;
    void applyPages();
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
    juce::Component listLayer_; // the list page: title area, rows, split button and footer, faded as one
    juce::Viewport viewport_;
    SplitButton split_;
    ModDotLinkButton disconnectAll_;
    std::unique_ptr<PortTargetSearchPage> searchPage_;
    std::unique_ptr<ModDotCanvasPicker> picker_;
    std::unique_ptr<ModDotCanvasPicker> retiredPicker_; // parked: stopPick can run inside the layer's own handler
    std::vector<PortTargetModule> pickTargets_;
    Footer footer_;
    int maxHeight_ = 0;
    int rowsTotal_ = 0;
    int desiredHeight_ = kTitleHeight + kSplitRowHeight + kBottomPad; // the list page's own height
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver anim_;
    juce::VBlankAnimatorUpdater pageUpdater_;
    AnimationDriver pageAnim_;
    bool searchOpen_ = false;
    float searchAmount_ = 0.0f;
    float pageFrom_ = 0.0f;
    float pageTo_ = 0.0f;
    bool heightFollowsFade_ = true; // false under Reduce Motion: the height is the new page's from the first frame
    juce::uint32 lastRefreshMs_ = 0;
    ExitEnterTimeline timeline_;
    bool motionActive_ = false;
    bool forceAnimate_ = false;
    bool reducedMotion_ = false;
    int focusTries_ = 0;
};

} // namespace synth::ui
