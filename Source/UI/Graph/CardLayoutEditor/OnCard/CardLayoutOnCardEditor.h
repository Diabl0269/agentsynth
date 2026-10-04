#pragma once

#include "CardLayoutAddPanel.h"
#include "CardLayoutControlPanel.h"
#include "CardLayoutEditBar.h"
#include "CardLayoutOutline.h"
#include "OnCardCells.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include "UI/Layout/ReorderDrag/ReorderFramePump.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
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
 * keeps the result, Cancel or Esc restores the layout the card opened with; the whole session is one
 * undo step, recorded when the source is destroyed. The bar's Preset and Apply to menus, and the "+ Add control"
 * strip the overlay adds under the card, are the other things it hosts; on an ADSR card the strip also holds the
 * "Time and tempo" switch, which rewrites the stages' group.
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
    const std::vector<oncard::Guide>& getGuidesForTest() const noexcept { return guides_; }
    bool isDraggingForTest() const noexcept { return drag_.moving; }
    bool hasPendingNudgeForTest() const noexcept { return !nudgeKey_.isEmpty(); }
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
        juce::Point<int> pressPoint;    // editor pixels
        juce::Rectangle<int> startRect; // the cell's rect at the press
        std::vector<int> sectionCells;  // indices of the cells sharing its section
        juce::Point<int> pressOffset;   // the pointer's offset from the cell's top-left at the press
    };

    // ---- Session and sync (CardLayoutOnCardEditor.cpp) -----------------------------------------
    ModuleComponent* findCard() const;
    void attachTo(ModuleComponent& card);
    void syncToCard();
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
    void commitMove(int cell, juce::Rectangle<int> dropped, juce::Rectangle<int> start, bool announceMove);
    void writeSection(int section, const std::vector<std::pair<int, juce::Rectangle<int>>>& rects);
    juce::Rectangle<int> clampToSection(int cell, juce::Rectangle<int> rect) const;
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
    void hideControl(const juce::String& paramId);
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

    // ---- Time and tempo (CardLayoutOnCardEditorTimeTempo.cpp) ------------------------------------
    void refreshTimeTempo();
    void chooseTimeTempo(int index);

    // ---- Keyboard (CardLayoutOnCardEditorKeyboard.cpp) -----------------------------------------
    bool handleKey(const juce::String& key, const juce::KeyPress& press);
    bool matchesAction(const juce::KeyPress& press, const char* actionId, const juce::KeyPress& fallback) const;
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

    juce::Component::SafePointer<CardLayoutControlPanel> panel_;
    juce::String panelParamId_;
    juce::Component::SafePointer<CardLayoutAddPanel> addPanel_;
    juce::TextButton addControl_{"+ Add control"};
    CardSegmentedSwitch timeTempo_{"Time and tempo", juce::StringArray{"Shared", "Separate"}};
    AddDrag addDrag_;
    bool applyToAll_ = false; ///< Every write goes to the type's default, not this module.

    CardLayoutEditBar editBar_;
    juce::OwnedArray<CardLayoutOutline> outlines_;
    std::vector<OnCardCell> cells_;

    Drag drag_;
    std::vector<oncard::Guide> guides_;
    ReorderCancelKey escapeKey_;
    juce::String nudgeKey_; ///< The control with a nudge not yet written; empty when none.
    juce::Rectangle<int> nudgeStart_;
    juce::String lastAnnouncement_;

    ReorderFramePump fadePump_{*this};
    ReorderFramePump glidePump_{*this};
    std::function<void()> finishGlide_; ///< Lands the glide in flight at once; empty when none.
    ReorderFramePump addFadePump_{*this};
    std::function<void()> finishAddFade_; ///< Lands the added control's fade-in at once; empty when none.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardLayoutOnCardEditor)
};

} // namespace synth::ui
