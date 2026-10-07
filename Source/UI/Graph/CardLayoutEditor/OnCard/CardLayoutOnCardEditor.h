#pragma once

#include "CardLayoutAddPanel.h"
#include "CardLayoutControlPanel.h"
#include "CardLayoutEditBar.h"
#include "CardLayoutHideZone.h"
#include "CardLayoutOutline.h"
#include "OnCardCells.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/ControlMotion.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class AppUndoManager;
class GraphEditor;
class ModuleComponent;
class ShortcutManager;

namespace synth::ui {

class BuiltInCardLayoutSource;

/**
 * "Edit Layout" on a module card (docs/layout/module-card-layout.md#editing-a-layout): the card itself
 * is the editor. A transparent overlay over the card, a child of the card's own parent, gives every
 * control a dashed outline with a grip to drag it anywhere in its group (guides, a dropped control
 * pushes the one under it aside) and arrow keys to nudge it. Each drop or settled nudge writes the
 * node's layout through a BuiltInCardLayoutSource, which rebuilds the card; the overlay then re-syncs
 * to the new card, so it holds the GraphEditor and the node id and never the card it covers. Done
 * keeps the result, Cancel or Esc restores the layout the card opened with. Every write is its own undo
 * step, recorded by the source as it is made, so Cmd+Z steps back one change at a time (the keys reach the
 * app, and the overlay re-syncs to the card the restore rebuilds); Cancel is one more step, which brings the
 * cancelled edits back. The bar's Preset and Apply to menus, and the "+ Add control"
 * strip the overlay adds under the card, are the other things it hosts; on an ADSR card the strip also holds the
 * "Controls" switch (Shared or Separate), which rewrites the stages' group; the card's own Sync switch
 * stays usable and picks the look being edited, and ending the session puts it back as it was.
 */
class CardLayoutOnCardEditor final
    : public juce::Component
    , private juce::ComponentListener
    , private juce::AsyncUpdater
    , private juce::Timer {
public:
    /** The strip under the card that holds "+ Add control": the overlay reaches this far past the card. */
    static constexpr int kAddStripHeight = 30;
    static constexpr int kTimeTempoWidth = 120;

    /** Opens an editor over node `nodeId`'s card and adds it to the card's parent; null when the card
     *  is not drawn from layout data. `shortcuts` may be null (the default keys apply). */
    static std::unique_ptr<CardLayoutOnCardEditor> open(GraphEditor& editor, ::AppUndoManager* undo,
                                                        juce::AudioProcessorGraph::NodeID nodeId,
                                                        const ShortcutManager* shortcuts);
    ~CardLayoutOnCardEditor() override;

    /** Runs once the session has ended and the overlay is off the canvas, for the owner to destroy it. */
    std::function<void()> onClosed;
    /** Told when a control's options are asked for (Return, double-click, right-click), as the panel opens. */
    std::function<void(const juce::String& paramId)> onControlOptions;

    /** Keeps the layout and ends the session. */
    void done();
    /** Restores the layout the card opened with and ends the session. */
    void cancel();
    bool isClosed() const noexcept { return closed_; }

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    /** The card's own Sync switch stays reachable under the overlay: the mouse passes through its cell. */
    bool hitTest(int x, int y) override;

    /** Test seam: takes each control panel instead of a call-out, as ModuleComponent's launcher takes the
     *  editor. The panel is the test's to keep; the editor only points at it while it is open. */
    static void setControlPanelLauncherForTest(std::function<void(std::unique_ptr<juce::Component>)> launcher);

    // ---- Test seams: read back or drive the real state -------------------------------------------
    int getOutlineCountForTest() const { return outlines_.size(); }
    CardLayoutOutline* getOutlineForTest(const juce::String& paramId) const;
    /** The control's cell on the card now (card pixels); empty when it has none. */
    juce::Rectangle<int> getCellRectForTest(const juce::String& paramId) const;
    /** The open control panel, or null once it is closed. */
    CardLayoutControlPanel* getControlPanelForTest() const { return panel_.getComponent(); }
    CardLayoutEditBar& getEditBarForTest() noexcept { return editBar_; }
    juce::TextButton& getAddButtonForTest() noexcept { return addControl_; }
    CardSegmentedSwitch& getTimeTempoSwitchForTest() noexcept { return timeTempo_; }
    /** The open Add control panel, or null once it is closed. */
    CardLayoutAddPanel* getAddPanelForTest() const { return addPanel_.getComponent(); }
    /** The Apply to and Preset menus as the bar's buttons would show them. */
    juce::PopupMenu buildApplyToMenuForTest() const { return buildApplyToMenu(); }
    juce::PopupMenu buildPresetMenuForTest() const { return buildPresetMenu(); }
    bool isApplyToAllForTest() const noexcept { return applyToAll_; }
    /** What the Save as window does once a name is typed. */
    void savePresetForTest(const juce::String& name) { savePreset(name); }
    /** The control's cell as the Add drag's ghost shows it now (overlay pixels); empty with no add drag. */
    juce::Rectangle<int> getAddGhostForTest() const noexcept { return addDrag_.ghost; }
    /** The control's home rect (where a push-free layout would put it); empty when it has none. */
    juce::Rectangle<int> getHomeRectForTest(const juce::String& paramId) const;
    const std::vector<oncard::Guide>& getGuidesForTest() const noexcept { return guides_; }
    bool isDraggingForTest() const noexcept { return drag_.moving; }
    /** The pointer is below the card mid-drag, so a release hides the control. */
    bool isHidingDragForTest() const noexcept { return drag_.hiding; }
    /** The "Drop to hide" area under the card, and how visible it is now (0 when no drag is live). */
    const CardLayoutHideZone& getHideZoneForTest() const noexcept { return hideZone_; }
    bool hasPendingNudgeForTest() const noexcept { return !nudgeKey_.isEmpty(); }
    /** Animates as if on screen (the editor is not showing in a test). */
    void setForceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    int getShrinkGhostCountForTest() const noexcept { return ghosts_.size(); }
    /** Puts every shrinking picture at progress `t` (0..1), as a frame would. */
    void setShrinkGhostProgressForTest(float t);
    /** Applies the added control's grow at progress `t` (0..1), as a frame would; nothing when none runs. */
    void applyAddFrameForTest(float t);
    /** A hidden control whose picture is still shrinking: the layout keeps it until then. */
    bool hasPendingHideForTest() const noexcept { return !pendingHide_.isEmpty(); }
    /** The same as the debounce expiring. */
    void flushNudgeForTest() { flushNudge(); }
    /** Esc as a drag in progress sees it; false when no drag is live. */
    bool sendEscapeToDragForTest();
    /** Lands every glide and fade at once (no frame arrives headless). */
    void finishMotionForTest();
    /** Runs the re-sync a card rebuilt from outside queued, now. */
    void runQueuedSyncForTest() { handleUpdateNowIfNeeded(); }
    const juce::String& getLastAnnouncementForTest() const noexcept { return lastAnnouncement_; }

private:
    CardLayoutOnCardEditor(GraphEditor& editor, ::AppUndoManager* undo, juce::AudioProcessorGraph::NodeID nodeId,
                           const ShortcutManager* shortcuts);

    struct Drag {
        int cell = -1;
        bool pressed = false;
        bool moving = false;
        int target = -1;                // the group under the pointer (plan section); the cell's own until it leaves
        bool hiding = false;            // the pointer is below the card: releasing hides the control
        juce::Point<int> pressPoint;    // editor pixels
        juce::Rectangle<int> startRect; // the cell's rect at the press
        juce::Point<int> pressOffset;   // the pointer's offset from the cell's top-left at the press
    };

    // ---- Session and sync (CardLayoutOnCardEditor.cpp) -----------------------------------------
    ModuleComponent* findCard() const;
    void attachTo(ModuleComponent& card);
    void syncToCard();
    void syncAfterOutsideChange();
    void animateRestore(const std::vector<OnCardCell>& before, const juce::Image& beforeImage, bool rebuilt);
    void rememberCardImage();
    void reconcileOutlines();
    void watchTabs(ModuleComponent& card);
    void wireOutline(CardLayoutOutline& outline);
    void close(bool keep);
    void finishClose();
    void fadeTo(float alpha, std::function<void()> done = {});
    void componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized) override;
    void componentBeingDeleted(juce::Component& component) override;
    void handleAsyncUpdate() override;
    int indexOfCell(const juce::String& key) const;

    // ---- Drag, drop and the write (CardLayoutOnCardEditorDrag.cpp) -------------------------------
    void pressOn(const juce::String& key, const juce::MouseEvent& e);
    void dragTo(const juce::MouseEvent& e);
    void releaseOn(const juce::MouseEvent& e);
    void moveCellTo(int cell, juce::Rectangle<int> rect);
    void cancelDrag();
    void endDrag();
    void setHideZoneShown(bool shown);
    void hideDragged(int cell);
    void commitMove(int cell, juce::Rectangle<int> dropped, juce::Rectangle<int> start, bool announceMove);
    juce::Rectangle<int> homeRectOf(int cell) const;
    std::vector<juce::Rectangle<int>> pushedNeighbours(int section, int except, juce::Rectangle<int> dropped,
                                                       juce::Rectangle<int> start) const;
    void refreshHomes();
    void writeSection(int section, const std::vector<std::pair<int, juce::Rectangle<int>>>& rects);
    juce::Rectangle<int> clampToSection(int cell, juce::Rectangle<int> rect) const;
    juce::Rectangle<int> clampToTarget(int section, juce::Rectangle<int> rect) const;
    juce::Rectangle<int> sectionArea(int section) const;
    void commitMoveToSection(int cell, juce::Rectangle<int> dropped, int target);
    std::vector<int> cellsOfSection(int section) const;

    /** A cell gliding from one rect to another over `ms`, easing in when it is on its way back. */
    struct Move {
        juce::String key;
        juce::Rectangle<int> from;
        juce::Rectangle<int> to;
        double ms = 160.0;
        bool easeIn = false;
    };
    void startGlide(std::vector<Move> moves);

    // ---- The per-control panel (CardLayoutOnCardEditorPanel.cpp) ---------------------------------
    void launchPanel(std::unique_ptr<juce::Component> panel, juce::Rectangle<int> screenArea);
    void openControlPanel(const juce::String& paramId);
    void closePanel();
    void refreshPanel();
    void writeLayout(const CardLayout& layout);
    void editControl(const juce::String& paramId, const std::function<CardLayout(CardLayout)>& edit);
    void hideControl(juce::String paramId); ///< By value: the id can belong to an outline the hide deletes.
    juce::Rectangle<int> screenAreaOf(const CardLayoutOutline& outline) const;

    // ---- Add control (CardLayoutOnCardEditorAdd.cpp, ...AddDrop.cpp) -----------------------------
    /** An Add control drag in flight: the control and where its cell would land now (overlay pixels). */
    struct AddDrag {
        juce::String paramId;
        juce::Rectangle<int> ghost;
    };
    void openAddPanel();
    void refreshAddPanel();
    void addControl(const juce::String& paramId);
    juce::Point<int> freeSpotIn(int planSection, const juce::String& paramId) const;
    void dragAdded(const juce::String& paramId, CardLayoutAddRow::DragPhase phase, juce::Point<int> screenPosition);
    void dropAdded(const juce::String& paramId, juce::Point<int> at);
    void finishAdded(const juce::String& paramId, const juce::String& name);
    void fadeInControl(const juce::String& paramId);
    bool startShrinkGhost(const juce::String& paramId);
    void startShrinkGhostOf(juce::Image image, juce::Rectangle<int> cell);
    void tickGhosts();
    void writeHide(juce::String paramId);
    void flushPendingHide();
    void dropPendingHide();
    bool canAnimate() const { return forceAnimateForTest_ || isShowing(); }
    int editableSectionAt(int y) const;
    juce::Point<int> addedCellSize(int planSection, const juce::String& paramId) const;
    std::vector<juce::Rectangle<int>> occupiedIn(int planSection) const;
    juce::Rectangle<int> cardArea() const { return getLocalBounds().withTrimmedBottom(kAddStripHeight); }

    // ---- Preset and Apply to (CardLayoutOnCardEditorScope.cpp) -----------------------------------
    juce::PopupMenu buildApplyToMenu() const;
    juce::PopupMenu buildPresetMenu() const;
    void chooseScope(bool allOfType);
    int cardsOfType() const;
    void loadPreset(const juce::String& name);
    void savePreset(const juce::String& name);
    void resetLayout();

    // ---- Controls: Shared | Separate (CardLayoutOnCardEditorTimeTempo.cpp) -----------------------
    void refreshTimeTempo();
    void chooseTimeTempo(int index);
    bool hasSyncLooks() const;
    juce::RangedAudioParameter* syncParameter() const;
    void rememberSync();
    void restoreSync();
    void flipSync();

    // ---- Keyboard (CardLayoutOnCardEditorKeyboard.cpp) -----------------------------------------
    bool handleKey(const juce::String& key, const juce::KeyPress& press);
    bool matchesAction(const juce::KeyPress& press, const char* actionId, const juce::KeyPress& fallback) const;
    bool isUndoOrRedo(const juce::KeyPress& press) const;
    bool isRemoveKey(const juce::KeyPress& press) const;
    void removeControl(const juce::String& paramId);
    void nudge(const juce::String& key, int dx, int dy);
    void flushNudge();
    void announce(const juce::String& text);
    void timerCallback() override;

    juce::Component::SafePointer<GraphEditor> graphEditor_;
    juce::AudioProcessorGraph::NodeID nodeId_;
    const ShortcutManager* shortcuts_;
    std::unique_ptr<BuiltInCardLayoutSource> source_;
    juce::Component::SafePointer<ModuleComponent> card_;
    const juce::Component* cardIdentity_ = nullptr; ///< Which card the listener is on; never dereferenced.
    bool closing_ = false;                          ///< close() is running: no write may glide.
    bool closed_ = false; ///< The session has ended; the overlay is only fading out or waiting to be destroyed.
    bool writing_ = false;
    bool cardRebuilt_ = false; ///< The card was deleted for a reason that is not ours, since the last re-sync.
    juce::Image cardImage_; ///< The card as of the last re-sync (2x), for a control an undo takes away to shrink from.

    juce::Component::SafePointer<CardLayoutControlPanel> panel_;
    juce::String panelParamId_;
    juce::Component::SafePointer<CardLayoutAddPanel> addPanel_;
    juce::TextButton addControl_{"+ Add control"};
    CardSegmentedSwitch timeTempo_{"Controls", juce::StringArray{"Shared", "Separate"}};
    std::optional<float> openingSync_; ///< The card's Sync (0..1) as the session opened; put back when it ends.
    AddDrag addDrag_;
    bool applyToAll_ = false; ///< Every write goes to the type's default, not this module.

    CardLayoutEditBar editBar_;
    CardLayoutHideZone hideZone_; ///< Fades in under the card while a control is dragged.
    ReorderFramePump hideZonePump_{*this};
    juce::OwnedArray<CardLayoutOutline> outlines_;
    std::vector<OnCardCell> cells_;
    /** Where each control stood before any push, by parameter id: set when the session opens or re-syncs to a
     *  restored card, moved only by the user's own drag or nudge of that control. A drop pushes its
     *  neighbours from these, so a control pushed aside returns as soon as its home is free. */
    std::map<juce::String, juce::Rectangle<int>> homes_;
    std::map<juce::String, juce::Rectangle<int>> origins_; ///< Where the layout first put each control.
    bool keepHomes_ = false; ///< A write of ours is re-syncing: pushed cells keep their homes.

    Drag drag_;
    std::vector<oncard::Guide> guides_;
    int dropSection_ = -1; ///< A group other than the dragged control's own that a drop would move it into.
    ReorderCancelKey escapeKey_;
    juce::String nudgeKey_; ///< The control with a nudge not yet written; empty when none.
    juce::Rectangle<int> nudgeStart_;
    juce::String lastAnnouncement_;

    ReorderFramePump fadePump_{*this};
    ReorderFramePump glidePump_{*this};
    std::function<void()> finishGlide_; ///< Lands the glide in flight at once; empty when none.
    ReorderFramePump addFadePump_{*this};
    std::function<void()> finishAddFade_; ///< Lands the added control's grow at once; empty when none.
    std::function<void(float)> addFrame_; ///< The added control's grow at a progress; empty when none runs.
    ReorderFramePump ghostPump_{*this};
    juce::OwnedArray<control_motion::ShrinkGhost> ghosts_; ///< Removed controls shrinking away.
    juce::String pendingHide_; ///< A hidden control not yet written: its picture is still shrinking.
    std::vector<juce::Component::SafePointer<juce::Component>> pendingHideParts_; ///< Its widget and caption.
    bool forceAnimateForTest_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutOnCardEditor)
};

} // namespace synth::ui
