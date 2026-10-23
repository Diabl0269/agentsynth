#pragma once

#include "SoundBrowser/SoundFolderBook.h"
#include "SoundBrowser/SoundIndex.h"
#include "SoundBrowser/SoundLists.h"
#include "SoundBrowser/SoundPreviewPlayer.h"
#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/KeyboardContextMenu.h"
#include "UI/Layout/NavigationSearchField.h"
#include "UI/Layout/NonModalLabel.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/SoundBrowser/SoundFilterChipRow.h"
#include "UI/Library/SoundBrowser/SoundFilterModel.h"
#include "UI/Library/SoundBrowser/SoundFilterPanel.h"
#include "UI/Library/SoundBrowser/SoundFiltersButton.h"
#include "UI/Library/SoundBrowser/SoundPreviewFooter.h"
#include <algorithm>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth::ui {

/** The Sounds view of the left sidebar: Places, folders, search results and a footer for the selected sound. The rows
 *  are painted and one accessibility list speaks the selected one, the same pattern as the module library. */
class SoundBrowserComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::FileDragAndDropTarget
    , public juce::SettableTooltipClient
    , public KeyboardContextMenuProvider
    , private juce::ScrollBar::Listener
    , private synth::soundpacks::SoundPackInstaller::Listener
    , private synth::soundbrowser::SoundFolderBook::Listener
    , private juce::FocusChangeListener
    , private juce::KeyListener
    , private synth::soundbrowser::SoundIndex::Listener {
public:
    static constexpr int kSearchHeight = 32;
    static constexpr int kPathHeight = 26;
    static constexpr int kRowHeight = 30;
    static constexpr int kFooterInfoHeight = 44; // the sound's name and place
    static constexpr int kFooterHeight = kFooterInfoHeight + SoundPreviewFooter::kHeight;
    static constexpr int kCompactFooterHeight = SoundPreviewFooter::kCompactHeight;
    static constexpr int kMinListRows = 2; // the list keeps this many rows before the footer or the panel give way
    static constexpr int kFiltersButtonWidth = 84;
    static constexpr int kDragStartThresholdPx = 4; // how far a pressed Sound row moves before it becomes a drag

    static constexpr const char* kFavouritesId = "@favourites";
    static constexpr const char* kRecentId = "@recent";

    /** GetPack is a pack you have not downloaded (greyed, with its size); AddFolder is the "Add folder..." row. */
    enum class RowKind { Hint, Place, Folder, Sound, GetPack, AddFolder };

    struct Row {
        RowKind kind = RowKind::Hint;
        juce::String text;            ///< Name shown (and matched by the search).
        juce::String detail;          ///< Right column.
        juce::String placeId, folder; ///< Where a Place or Folder row leads.
        int soundCount = 0;
        std::optional<synth::soundbrowser::SoundEntry> entry; ///< Sound rows only.
        juce::String packId;                                  ///< GetPack rows: the hosted pack.
        juce::int64 bytes = 0;                                ///< GetPack rows: the download size.
        float progress = -1.0f;                               ///< GetPack rows while busy: 0 to 1, negative = unknown.
        bool busy = false;                                    ///< GetPack rows: downloading, checking or installing.
        bool cancellable = false;                             ///< GetPack rows: the download can still be stopped.
        bool viaInstrument = false;                           ///< GetPack rows in search: matched an instrument name.
        bool userFolder = false;                              ///< Place rows: one of the user's own folders.
        bool missing = false;                                 ///< Place rows: the folder is not there now.
        bool scanning = false;                                ///< Place rows: its sounds are still being found.
        bool navigable() const { return kind != RowKind::Hint; }
    };

    struct PathPart {
        juce::String label, placeId, folder;
        juce::Rectangle<int> bounds;
    };

    SoundBrowserComponent(synth::soundbrowser::SoundIndex& index, synth::soundbrowser::SoundLists& lists);
    ~SoundBrowserComponent() override;

    /** Lists the packs the installer offers but this device lacks ("Get more") and starts their downloads. Null (the
     *  default) lists none. The installer must outlive the view. */
    void setPackInstaller(synth::soundpacks::SoundPackInstaller* installer);
    /** Shows "Add folder..." and the folder menu, and edits the user's folders through `book`. Null shows neither. */
    void setFolderBook(synth::soundbrowser::SoundFolderBook* book);

    /** A sound row became the selection (arrow, click). */
    std::function<void(const synth::soundbrowser::SoundEntry&)> onSoundSelected;
    /** Return or a double-click on a sound row. */
    std::function<void(const synth::soundbrowser::SoundEntry&)> onSoundActivated;

    // ---- Filters (SoundBrowserFilters.cpp) ----------------------------------------------------------------
    synth::ui::SoundFilterModel& getFilters() { return filters_; }
    bool areFiltersOpen() const { return filtersOpen_; }
    /** Opens or closes the Filters panel under the search; it pushes the list down and grows 190 ms. */
    void setFiltersOpen(bool open, bool animate = true);
    /** The filters changed from outside the panel (a test, a restore): chips, panel and list follow. */
    void applyFiltersChanged();
    /** The song's key when the project has one; "" otherwise. Adds the Song key chip to the Key group. */
    void setSongKey(const juce::String& key);
    /** The song's tempo for the Near song tempo filter; when unset it reads 120. */
    std::function<double()> songTempoProvider;
    /** Where the list starts now: below the search, the chips, the open panel and the path bar. */
    int getListTop() const { return listTop_; }
    SoundFiltersButton& getFiltersButton() { return filtersButton_; }
    SoundFilterPanel& getFilterPanel() { return filterPanel_; }
    /** The viewport the panel scrolls in when the column is too short to show all of it. */
    juce::Viewport& getPanelViewport() { return panelHost_; }
    SoundFilterChipRow& getChipRow() { return chipRow_; }
    bool isFilterMotionRunning() const { return panelAnim_.isRunning() || chipRow_.isMotionRunning(); }
    void completeFilterMotionForTest();
    void setAnimateOffscreenForTest(bool on);

    // ---- Preview (SoundBrowserPreview.cpp) ------------------------------------------------------------------
    /** The audio engine's preview player (may be null: the view then plays nothing). Applies the preview volume. */
    void setPreviewPlayer(synth::soundbrowser::SoundPreviewPlayer* player);
    /** Auto-play and the preview volume as persisted; does not call onPreviewSettingsChanged. */
    void setPreviewSettings(bool autoPlay, float gainDb);
    bool isAutoPlay() const { return footer_.isAutoPlay(); }
    float getPreviewGainDb() const { return footer_.getGainDb(); }
    /** Auto-play or the preview volume was changed by the user. */
    std::function<void()> onPreviewSettingsChanged;
    /** Plays the selected sound (a loop on the next bar when the song plays); false when none can play. */
    bool playSelected();
    void stopPreview();
    /** Space: stops the preview when one sounds, else plays the selected sound. */
    bool togglePreview();
    SoundPreviewFooter& getPreviewFooter() { return footer_; }
    /** What the footer shows at this height: the whole strip, the one-row strip, or nothing. */
    enum class FooterMode { Full, Compact, Hidden };
    FooterMode getFooterMode() const { return footerMode_; }

    // ---- Dragging a sound (SoundBrowserDrag.cpp) ----------------------------------------------------------------
    /** Starts dragging the Sound row `index` as the drag-and-drop payload `sound:<path>`; false for any other row. */
    bool beginSoundDrag(int index);
    bool isSoundDragging() const { return soundDragging_; }

    // ---- Packs you do not have (SoundBrowserPacks.cpp) ----------------------------------------------------------
    /** Starts the shared installer's download of GetPack row `index`, or cancels it while it runs; false for any other
     *  row or when the installer refuses. */
    bool toggleDownload(int index);
    /** Where GetPack row `index`'s Download (Cancel) button is painted, in this view's coordinates. */
    juce::Rectangle<int> getPackButtonBounds(int index) const;

    // ---- The user's folders (SoundBrowserFolders.cpp) -----------------------------------------------------------
    enum class FolderAction { Rename, ShowInFinder, ScanAgain, Remove };
    /** Opens the folder chooser; the folder picked is added. */
    void chooseFolderToAdd();
    /** Adds each existing folder to the book and selects the last one added; false when none was new. */
    bool addFolders(const juce::Array<juce::File>& folders);
    /** The right-click menu of a user folder's Place row: Rename, Show in Finder (File Explorer on Windows), Scan
     * again, Remove. */
    juce::PopupMenu buildFolderMenu(int rowIndex) const;
    /** Opens that menu at the pointer (`anchor` empty) or at `anchor`, a screen area. */
    void showFolderMenu(int rowIndex, std::optional<juce::Rectangle<int>> anchor);
    /** Does what a menu item does to the user folder shown at `rowIndex`; false for any other row. */
    bool applyFolderAction(int rowIndex, FolderAction action);
    bool showContextMenuForKeyboardFocus() override;
    /** The inline editor over a folder's name; visible only while renaming. */
    NonModalLabel& getRenameLabel() { return renameLabel_; }
    bool isFolderDropActive() const { return dropActive_; }
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    /** Stat-checks the folders when the app comes back to the front (and when the view shows): a drive that was
     *  unplugged or plugged in rescans, nothing else does. */
    void checkFoldersOnReturn();

    // Test seams: the native pieces. `picker` answers the folder chooser with a folder (or none); `reveal` stands in
    // for Finder; `foreground` for juce::Process::isForegroundProcess.
    void setFolderPickerForTest(std::function<void(std::function<void(const juce::File&)>)> picker) {
        folderPicker_ = std::move(picker);
    }
    void setRevealForTest(std::function<void(const juce::File&)> reveal) { reveal_ = std::move(reveal); }
    void setForegroundCheckForTest(std::function<bool()> foreground) { foregroundCheck_ = std::move(foreground); }
    void noteAppFocusForTest() { noteAppFocus(); }

    /** The control last asked to take focus by an Up or Down step (the list is this view); headless windows cannot
     *  hold real focus, so tests read it here. */
    juce::Component* lastFocusRequestForTest() const { return lastFocusRequest_; }
    /** Whether Up and Down from `control` reach this view's stepping (it is a key listener there). */
    bool listensForStepKeysForTest(juce::Component& control) const {
        return std::find(reachAttached_.begin(), reachAttached_.end(), &control) != reachAttached_.end();
    }
    /** How many times any view was handed the keyboard while it was being destroyed (it must stay 0). */
    static int focusGainsWhileTearingDownForTest();
    /** Offers `key` to the stepping as `origin`'s key listener: what the peer does before origin's own keyPressed. */
    bool offerStepKeyForTest(const juce::KeyPress& key, juce::Component& origin) {
        return listensForStepKeysForTest(origin) && keyPressed(key, &origin);
    }

    void focusSearchField();
    void focusList();
    bool isSearchActive() const { return query_.trim().isNotEmpty(); }
    void setQuery(const juce::String& text);
    synth::ui::NavigationSearchField& getSearchField() { return search_; }

    /** Where the list is: a place id ("" = the Places screen, kFavouritesId, kRecentId, a pack id or "folder:...")
     *  and a '/'-separated folder inside it. `animateDirection` slides the old list out: +1 in, -1 out, 0 fade. */
    void showLocation(const juce::String& placeId, const juce::String& folder, int animateDirection = 0);
    const juce::String& getPlaceId() const { return placeId_; }
    const juce::String& getFolder() const { return folder_; }
    std::vector<PathPart> getPathParts() const;
    juce::String getPathText() const;

    const std::vector<Row>& getRows() const { return rows_; }
    int getSelectedIndex() const { return selected_; }
    void selectRow(int index);
    int getRowIndexAt(juce::Point<int> p) const;
    juce::Rectangle<int> getRowBounds(int index) const;
    /** Where the rows are: under the search, chips, panel and path bar, above the footer. */
    juce::Rectangle<int> listArea() const;
    juce::Rectangle<int> getPathPartBounds(int partIndex) const;
    bool openSelected();
    bool goUp();
    int getScrollOffset() const { return scrollOffset_; }

    juce::String getAccessibilityValueText() const;
    bool isListAnimating() const { return listAnim_.isRunning(); }
    void completeListAnimationForTest() { listAnim_.completeForTest(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void visibilityChanged() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void soundIndexChanged() override;
    void soundPacksChanged() override;
    void soundFoldersChanged() override;
    void globalFocusChanged(juce::Component* focused) override;

    // Rows (SoundBrowserNavigation.cpp)
    void rebuildRows();
    void rebuildPlaceRows(const synth::soundbrowser::SoundSnapshot& snap);
    void rebuildPlaceListing(const synth::soundbrowser::SoundSnapshot& snap, const synth::soundbrowser::SoundFilter&);
    void appendSoundRows(const std::vector<synth::soundbrowser::SoundEntry>& entries);
    juce::String selectionKey(int index) const;
    void restoreSelection(const juce::String& key);
    void navigateTo(const juce::String& placeId, const juce::String& folder, int direction,
                    const juce::String& selectKey);
    juce::String placeNameOf(const juce::String& placeId) const;
    void capturePreviousList();
    void playListMotion(int direction);

    // Painting (SoundBrowserPainting.cpp)
    void paintRows(juce::Graphics& g, int width) const;
    void paintRow(juce::Graphics& g, const Row& row, int index, juce::Rectangle<int> area) const;
    void paintPackRow(juce::Graphics& g, const Row& row, int index, juce::Rectangle<int> area) const;
    void paintAddFolderRow(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintPlaceRow(juce::Graphics& g, const Row& row, juce::Rectangle<int> area) const;
    bool activateSelectedAction();
    void paintPathBar(juce::Graphics& g, juce::Colour muted, juce::Colour primary, juce::Colour accent);
    void paintFooter(juce::Graphics& g);
    void paintCompactName(juce::Graphics& g);
    int contentWidth() const;
    void layoutPathParts();
    void updateScrollBar();
    void scrollSelectionIntoView();
    bool setScrollOffset(int offset);
    void applySearchColours();

    // Filters (SoundBrowserFilters.cpp)
    void setupFilters();
    void layoutTop();
    void filtersChanged();
    void refreshFilterButton();
    void refreshPlaces();
    int chipRowHeight() const;
    int footerHeight() const;
    /** What gives way in a short column: the footer's mode and the height kept for the list. */
    struct Stack {
        FooterMode footer = FooterMode::Full;
        int listReserve = 0;
    };
    Stack chooseStack(int heightAbove) const;
    void layoutFooter();
    void layoutPanel(int chipHeight);
    void scrollFocusedControlIntoView(juce::Component* focused);
    bool showsFlatResults() const;
    synth::soundbrowser::SoundFilter currentFilter();
    void finishFilterClose();

    // Keyboard reach (SoundBrowserKeyboardReach.cpp)
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
    std::vector<juce::Component*> headerStops();
    void attachReachListeners();
    void focusStop(juce::Component& stop);
    bool climbToHeader();
    bool atListEnd(int direction) const;
    bool enterFooter();
    void focusListStop(bool selectFirst);
    std::vector<juce::Component*> footerStops();

    // Packs (SoundBrowserPacks.cpp)
    std::vector<synth::soundpacks::SoundPackInstaller::Row>
    offeredPacks(const synth::soundbrowser::SoundSnapshot&) const;
    void appendOfferedPackRows(const synth::soundbrowser::SoundSnapshot& snap, bool places);
    void refreshPackRows();
    juce::String packSignature() const;
    juce::String packTooltip(const Row& row) const;
    bool packButtonHit(int index, juce::Point<int> p) const;

    // Folders (SoundBrowserFolders.cpp)
    void setupFolders();
    bool beginRename(int rowIndex);
    void commitRename();
    void endRename();
    void setDropActive(bool active);
    void noteAppFocus();
    void paintDropOutline(juce::Graphics& g) const;

    // Preview (SoundBrowserPreview.cpp)
    void setupPreview();
    void syncFooterToSelection();

    // Drag (SoundBrowserDrag.cpp)
    juce::ScaledImage makeSoundDragImage(const juce::String& name) const;

    // Input (SoundBrowserInput.cpp)
    bool handleSearchKey(const juce::KeyPress& key);
    bool moveSelection(int delta);
    void jumpToSearchWith(juce::juce_wchar character);
    void toggleSelectedFavourite();
    void refreshAccessibilityValue();
    void notifySelected();
    juce::String tooltipForRow(int index) const;

    synth::soundbrowser::SoundIndex& index_;
    synth::soundbrowser::SoundLists& lists_;
    synth::soundpacks::SoundPackInstaller* installer_ = nullptr;
    synth::soundbrowser::SoundFolderBook* folderBook_ = nullptr;
    juce::String packSignature_;
    std::function<void(std::function<void(const juce::File&)>)> folderPicker_;
    std::function<void(const juce::File&)> reveal_;
    std::function<bool()> foregroundCheck_;
    std::unique_ptr<juce::FileChooser> chooser_;
    NonModalLabel renameLabel_;
    juce::File renaming_;
    juce::String pendingSelection_; // a row to select once it exists (a folder just added)
    bool appWasForeground_ = true;
    int packButtonHot_ = -1; // the GetPack row whose button the pointer is over
    bool dropActive_ = false;
    float dropGlow_ = 0.0f;
    synth::ui::AnimationDriver dropAnim_;

    synth::ui::NavigationSearchField search_;
    SoundFilterModel filters_;
    SoundFiltersButton filtersButton_;
    SoundFilterChipRow chipRow_;
    juce::Viewport panelHost_;
    SoundFilterPanel filterPanel_{filters_};
    SoundPreviewFooter footer_;
    synth::soundbrowser::SoundPreviewPlayer* previewPlayer_ = nullptr;
    std::uint64_t lastRequestId_ = 0;
    bool filtersOpen_ = false;
    FooterMode footerMode_ = FooterMode::Full;
    int listReserve_ = kMinListRows * kRowHeight; // list height kept before the open panel may take it
    int listTop_ = kSearchHeight + kPathHeight;
    bool animateOffscreenForTest_ = false;
    juce::Component* lastFocusRequest_ = nullptr;
    bool tearingDown_ = false;
    std::vector<juce::Component*> reachAttached_;
    PanelSlide panelSlide_;
    synth::ui::AnimationDriver panelAnim_;
    juce::ScrollBar scrollBar_{true};
    juce::String query_;
    juce::String placeId_; // "" = Places
    juce::String folder_;
    std::vector<Row> rows_;
    std::vector<PathPart> pathParts_;
    int selected_ = -1;
    int hovered_ = -1;
    int pressedSound_ = -1; // the Sound row under the press, until it becomes a drag or the button goes up
    bool soundDragging_ = false;
    int hoveredPart_ = -1;
    int scrollOffset_ = 0;
    juce::String announcedValueText_;

    // The slide (or fade) between two lists: the old one as a picture, the new one live.
    std::optional<juce::VBlankAnimatorUpdater> vblank_;
    synth::ui::AnimationDriver listAnim_;
    juce::Image previousList_;
    float listMotion_ = 1.0f;
    int motionDirection_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundBrowserComponent)
};

} // namespace synth::ui
