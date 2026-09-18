#pragma once

#include "Plugin/Hosting/HostedPluginBackend.h"
#include "ShortcutManager/ShortcutManager.h"
#include "SnippetManager.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/ModuleLibraryHelpPopup.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

class ModuleLibraryComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::SettableTooltipClient
    , private juce::ScrollBar::Listener
    , private juce::KeyListener {
public:
    /** What a row in the sidebar is, which decides how it paints and what a click does. */
    enum class RowKind {
        Header,    // category title — clicking it collapses/expands the section
        SubHeader, // non-interactive sub-label inside a section, e.g. a plugin format group ("VST3")
        Module,    // draggable module name
        Snippet,   // draggable saved group (issue #156)
        Plugin,    // draggable scanned third-party plugin
        Action,    // clickable command row, e.g. "Scan for plugins…"
        EmptyHint  // non-interactive placeholder, e.g. "No snippets yet"
    };

    struct Entry {
        juce::String text;
        bool isHeader = false;
        RowKind kind = RowKind::Module;
        juce::String section; // text of the header this row lives under ("" for headers)
        int moduleCount = 0;  // Snippet rows only — shown as a count suffix
        juce::String detail;  // Plugin rows only — the format tag drawn on the right ("VST3")
        int pluginUid = 0;    // Plugin rows only — completes the identity the drag payload carries
    };

    // ---- Row geometry (pixels) ----
    static constexpr int kSearchHeight = 32;   // search field pinned at the top of the sidebar
    static constexpr int kTopStripHeight = 24; // "Collapse all / Expand all" chrome below the search field
    static constexpr int kPinnedChromeHeight = kSearchHeight + kTopStripHeight;
    static constexpr int kFirstRowY = 10; // gap between the pinned chrome and the first entry row
    static constexpr int kHeaderHeight = 25;
    static constexpr int kHeaderGap = 5; // extra breathing room above every header but the first
    static constexpr int kItemHeight = 32;

    // The "?" help button sharing the collapse-all strip's row — see getHelpButtonBounds().
    static constexpr int kHelpButtonSize = 18;
    static constexpr int kHelpButtonMargin = 6;

    /** Inclusive [start, start+length) range of a case-insensitive query hit inside a label. */
    struct HighlightSpan {
        int start = 0;
        int length = 0;
    };

    static constexpr const char* kSnippetsHeader = "Snippets";
    static constexpr const char* kPluginsHeader = "Plugins";
    /** The one row the Plugins section always has: the scan trigger, and — when nothing has been
     *  scanned yet — the only thing in the section, so it doubles as the empty-state hint. */
    static constexpr const char* kScanPluginsRowText = "Scan for plugins...";

    ModuleLibraryComponent();

    ~ModuleLibraryComponent() override;

    // -------------------------------------------------------------------------
    // Snippets (issue #156)
    // -------------------------------------------------------------------------

    /** Replaces the Snippets section contents. Called by the owner after any snippet is saved or
     *  deleted; the sidebar itself never touches the filesystem. */
    void setSnippets(const juce::Array<synth::SnippetInfo>& newSnippets);

    int getSnippetCount() const noexcept { return snippets.size(); }

    /** Invoked when the user picks "Delete Snippet" from a snippet row's context menu. */
    std::function<void(const juce::String&)> onSnippetDeleteRequested;

    // -------------------------------------------------------------------------
    // Search
    // -------------------------------------------------------------------------

    /** Trimmed query: empty means the library is unfiltered. */
    static juce::String normalisedSearchQuery(const juce::String& raw) { return raw.trim(); }

    static bool textMatchesQuery(const juce::String& text, const juce::String& query);

    /** Non-overlapping case-insensitive hits of `query` inside `text`, in left-to-right order. */
    static std::vector<HighlightSpan> highlightSpansFor(const juce::String& text, const juce::String& query);

    void setSearchText(const juce::String& text);

    juce::String getSearchText() const { return searchEditor.getText(); }

    bool isSearchActive() const { return normalisedSearchQuery(searchQuery).isNotEmpty(); }

    /** T160: grabs keyboard focus on the search field specifically — the destination
     *  `AppCommands::focusLibrarySearch` (Cmd+F) needs, distinct from `FocusRegionRegistry`'s
     *  region-root focus (Cmd+Shift+L lands on `this`, not the search field; see FocusRegion.h /
     *  docs/shortcuts.md's Focus regions section). The caller (MainComponent) is responsible for
     *  opening the Library first if it is closed, mirroring every other direct-focus shortcut. */
    void focusSearchField() { searchEditor.grabKeyboardFocus(); }

    // -------------------------------------------------------------------------
    // Plugins
    //
    // The sidebar knows nothing about scanning: it is handed a list of identities and hands back
    // two callbacks. That keeps PluginScanService (Core, background threads, child processes) out of
    // a GUI component entirely, and it is why this section is exercisable headlessly — a test calls
    // setPlugins() and activateRow() without a scan ever happening.
    //
    // A row carries the IDENTITY (format + uid + name), never a file path: the drag payload is read
    // by whatever component the user drops on, and a path on that channel would undo the whole point
    // of PluginIdentity. Resolving identity -> binary stays inside the scan list.
    // -------------------------------------------------------------------------

    /** Replaces the Plugins section contents. Called by the owner on startup and after every scan. */
    void setPlugins(const std::vector<synth::PluginIdentity>& newPlugins);

    int getPluginCount() const noexcept { return (int)plugins.size(); }

    /** Fired when the user clicks the "Scan for plugins..." row. */
    std::function<void()> onScanPluginsRequested;

    /** Fired when the user clicks (rather than drags) a plugin row — the owner adds the module at a
     *  sensible canvas position. Dragging goes through the DragAndDrop payload instead. */
    std::function<void(const synth::PluginIdentity&)> onPluginActivated;

    /** T160: fired by Enter-to-insert on a keyboard-focused Module row. Module rows had NO
     *  click-to-add path before this — mouseDown() starts a drag immediately for them (see below),
     *  so mouseUp()/activateRow() was never reached for RowKind::Module until now. The owner adds
     *  the module at a sensible canvas position, mirroring onPluginActivated. Never fired for a
     *  disabled (already-in-patch singleton) row — see isEntryEnabled(). */
    std::function<void(const juce::String&)> onModuleActivated;

    /** T160: fired by Enter-to-insert on a keyboard-focused Snippet row — same "no prior
     *  click-to-add path" gap as onModuleActivated above. Snippet rows are never gated by
     *  isModuleAvailable, so unlike onModuleActivated this fires unconditionally. */
    std::function<void(const juce::String&)> onSnippetActivated;

    /** Performs the click action for the row at `index`: fires the scan request for the Action row,
     *  onPluginActivated for a Plugin row, or (T160) onModuleActivated/onSnippetActivated for a
     *  Module/Snippet row. No-op for anything else. Public so the behaviour is reachable without
     *  synthesising mouse events — this is also what keyPressed()'s Enter-to-insert calls. */
    void activateRow(int index);

    /** The identity a Plugin row stands for; an invalid identity for any other row. */
    synth::PluginIdentity getPluginIdentity(int index) const;

    // -------------------------------------------------------------------------
    // Collapse / expand
    // -------------------------------------------------------------------------

    bool isSectionCollapsed(const juce::String& header) const;

    void setSectionCollapsed(const juce::String& header, bool collapsed);

    void toggleSection(const juce::String& header) { setSectionCollapsed(header, !isSectionCollapsed(header)); }

    /** True when every section is collapsed — drives the top strip's label and its action. */
    bool areAllSectionsCollapsed() const;

    /** Collapses every section, or expands every section when they are already all collapsed. */
    void toggleAllSections() { setAllSectionsCollapsed(!areAllSectionsCollapsed()); }

    void setAllSectionsCollapsed(bool collapsed);

    /** Collapsed section names, for persistence by the owner. */
    juce::StringArray getCollapsedSections() const;

    /** Restores a persisted collapse state. Does NOT fire onCollapseStateChanged — this IS the
     *  restore path, and re-notifying would write back what we just read. */
    void setCollapsedSections(const juce::StringArray& headers);

    /** Fired whenever the collapse state changes through user interaction, so the owner can
     *  persist it. */
    std::function<void()> onCollapseStateChanged;

    // -------------------------------------------------------------------------
    // Collapse animation
    // -------------------------------------------------------------------------

    static constexpr double kCollapseAnimMs = 150.0;

    /** How far a section is folded: 0 = fully open, 1 = fully closed. Between those while the
     *  accordion is animating. The *logical* state stays in `collapsedSections` and flips
     *  instantly, so `isSectionCollapsed()`, persistence and `areAllSectionsCollapsed()` never
     *  lag behind the visuals. */
    float getSectionProgress(const juce::String& header) const;

    /** Sets the visual fold amount directly, without touching the logical collapse state.
     *  Normally the animation owns this; it is exposed so the accordion geometry can be exercised
     *  at intermediate values, which a VBlank-driven clock cannot produce headlessly. */
    void setSectionProgress(const juce::String& header, float progress);

    bool isCollapseAnimating() const noexcept { return collapseAnim.isRunning(); }

    /** Drops any in-flight animation onto its final layout. */
    void finishCollapseAnimation();

    /** Optional predicate deciding whether a module can currently be added. Used for the singleton
     *  I/O modules: once a patch has an Audio Output, its row greys out and stops being draggable,
     *  rather than accepting a drag that would silently do nothing. Unset means everything is
     *  available, which keeps headless tests and every non-singleton module unaffected. */
    std::function<bool(const juce::String&)> isModuleAvailable;

    /** ShortcutManager whose LIVE bindings back the "Key shortcuts" section of the help popover
     *  (see showHelpPopover() / createHelpPopupForTest()) — read-only, the sidebar never rebinds
     *  anything. Optional: null (the default — every headless test, and any owner that has not
     *  wired one yet) falls back to each curated shortcut's shipped default binding, via
     *  shortcutHintFor's own null-manager contract. */
    void setShortcutManager(const ShortcutManager* manager) noexcept { shortcutManager = manager; }

    /** True when the row at `index` is a draggable row that can currently be added. Snippet rows are
     *  draggable but never gated — the predicate only ever describes module types. */
    bool isEntryEnabled(int index) const;

    // -------------------------------------------------------------------------
    // Pure static helpers — callable headlessly (no GUI / MessageManager needed)
    // -------------------------------------------------------------------------

    /** Composite collapse-state key for a plugin-format sub-group (e.g. "VST3" under Plugins),
     *  kept independent from the top-level header key so folding a format group never touches
     *  (or is touched by) the Plugins header's own fold. The separator never round-trips through
     *  the UI — it only appears inside collapsedSections / sectionProgress map keys and the
     *  opaque StringArray persistence in MainComponent, which is already schema-free. */
    static juce::String subsectionKey(const juce::String& section, const juce::String& subHeader);

    /** Returns a one-line description for a known module name, or a generic
     *  fallback string for unknown names. */
    static juce::String descriptionFor(const juce::String& moduleName);

    /** Tooltip for a saved snippet row. */
    static juce::String snippetDescription(const juce::String& name, int moduleCount);

    /** Tooltip for a scanned plugin row. */
    static juce::String pluginDescription(const juce::String& name, const juce::String& format);

    /** Tooltip for the scan row. */
    static juce::String scanRowDescription(bool anyPluginsKnown);

    // -------------------------------------------------------------------------
    // Layout
    // -------------------------------------------------------------------------

    struct Row {
        int entryIndex;
        int y;
        int height;
    };

    /** Visible rows, top to bottom. Painting and hit-testing share this one layout pass, so they
     *  cannot disagree about where a row is — they used to duplicate the y-advance arithmetic.
     *
     *  Each section's rows live in a band whose height is its natural height scaled by
     *  (1 - collapse progress), the way `height: auto → 0; overflow: hidden` behaves. Rows keep
     *  their natural spacing inside the band and are *truncated* at its bottom edge rather than
     *  squashed, so text never distorts mid-animation; `row.height` below `kItemHeight` means the
     *  row is partly clipped, and rows past the band are dropped (so they stop hit-testing too).
     *  At progress 0 and 1 this reduces exactly to the un-animated layout.
     *
     *  An active search hides rows whose names (or whose section header) do not contain the query,
     *  drops empty sections, and treats remaining sections as fully open so matches are not trapped
     *  inside a fold. Collapse state itself is left alone — clearing the query restores it. */
    std::vector<Row> buildRows() const;

    /** Total pixel height of the currently visible content. */
    int getTotalContentHeight() const;

    /** True when y falls inside the collapse-all chrome (below the search field). */
    static bool isInTopStrip(int y) noexcept { return y >= kSearchHeight && y < kPinnedChromeHeight; }

    /** True when y falls inside the pinned search field or the collapse-all strip. */
    static bool isInPinnedChrome(int y) noexcept { return y >= 0 && y < kPinnedChromeHeight; }

    // -------------------------------------------------------------------------
    // Scrolling
    //
    // The library is one painted component rather than a Viewport + inner content: rows are drawn
    // from a single buildRows() pass, and a Viewport would mean splitting that (plus the tooltip
    // client and the drag source) across two components. Instead the rows are drawn through a
    // scrollOffset and a juce::ScrollBar drives it. The search field and COLLAPSE ALL strip stay
    // pinned, so the two controls that change which rows are on screen never scroll out of reach.
    // -------------------------------------------------------------------------

    /** Rows scroll inside the panel below the pinned chrome. Both the content and the viewport
     *  lose the same kPinnedChromeHeight, so the maximum offset is just the plain overflow. */
    int getMaxScrollOffset() const { return juce::jmax(0, getTotalContentHeight() - juce::jmax(0, getHeight())); }

    int getScrollOffset() const noexcept { return scrollOffset; }

    /** Scrolls to `newOffset`, clamped to [0, getMaxScrollOffset()]. Returns true when it moved. */
    bool setScrollOffset(int newOffset);

    /** True when the rows overflow the panel and the scrollbar is therefore on screen. */
    bool isScrollBarVisible() const noexcept { return verticalScrollBar.isVisible(); }

    void resized() override;

    void lookAndFeelChanged() override;

    void parentHierarchyChanged() override;

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // -------------------------------------------------------------------------
    // Paint
    // -------------------------------------------------------------------------

    void paint(juce::Graphics& g) override;

    // T159: focus-region outline (Source/UI/Layout/FocusRegion.h), drawn OVER children like GraphEditor's
    // own outline -- paint() alone isn't enough here: the scrollable row content and the top strip
    // tile right up to the panel's own edge, so an outline drawn at the end of paint() would sit
    // UNDER whatever gets painted next and never actually show.
    void paintOverChildren(juce::Graphics& g) override { synth::ui::paintFocusRegionOutline(*this, g); }

    // -------------------------------------------------------------------------
    // Mouse events
    // -------------------------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override;

    void mouseExit(const juce::MouseEvent&) override;

    void mouseDown(const juce::MouseEvent& e) override;

    void mouseDrag(const juce::MouseEvent& e) override;

    void mouseUp(const juce::MouseEvent& e) override;

    // -------------------------------------------------------------------------
    // Keyboard navigation (T160)
    //
    // Two entry points feed the same three handlers below, because keyboard focus can genuinely be
    // in two different places: `this` itself (Cmd+Shift+L / Tab-cycle land here, per FocusRegion.h)
    // or `searchEditor` (Cmd+F, via focusSearchField()). keyPressed() below only ever runs while
    // `this` holds real focus; the searchEditor case is handled by the KeyListener override further
    // down, registered on searchEditor in the constructor, because a single-line juce::TextEditor's
    // own keyPressed() UNCONDITIONALLY consumes Up/Down/Return itself (moveCaretUp/Down collapse to
    // moveCaretToStartOfLine/EndOfLine for a single-line editor, and moveCaretWithTransaction always
    // returns true) — they never bubble out, so intercepting them ahead of the editor via a key
    // listener is the only way to reach row navigation from the search field at all.
    // -------------------------------------------------------------------------

    bool keyPressed(const juce::KeyPress& key) override;

private:
    /** The KeyListener half of the scheme above — see the class comment. Only Up/Down/Return are
     *  intercepted; Left/Right are deliberately left alone so the caret still moves through the
     *  typed query, and Tab is left alone so it keeps bubbling to MainComponent's focusNextRegion
     *  cycle untouched (TextEditor's own keyPressed already returns false for Tab — tabKeyUsed
     *  defaults false and no key-function table entry claims it — so no explicit handling is needed
     *  here to keep that path open; a listener that intercepted it would be the one thing that could
     *  break it). Ignores every originatingComponent except searchEditor, since KeyListener
     *  notifications for a key pressed anywhere else in this component's subtree would otherwise
     *  double up with keyPressed() above once ancestor bubbling reaches `this`. */
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    /** Every visible row Up/Down can land on and Enter can activate — every kind except EmptyHint
     *  (a non-interactive placeholder with nothing to do). Includes Header/SubHeader, unlike
     *  isInteractiveEntry(), because Left/Right needs to be able to fold/expand a section from the
     *  keyboard the same way clicking its chevron does. */
    bool isKeyboardNavigableEntry(int index) const;

    std::vector<int> navigableEntryIndices() const;

    /** Moves keyboardFocusedIndex by `delta` steps (+1/-1) through the currently visible navigable
     *  rows, clamped at either end (no wraparound — landing back at the opposite end of a long,
     *  scrolled list would be disorienting). Starting from no focus (-1) lands on the first row for
     *  a downward move, the last for an upward one, matching the natural "search, then arrow down
     *  into the results" flow. Always returns true when there is anything to navigate, so the key
     *  never bubbles into stray behaviour elsewhere (a bare Up/Down reaching MainComponent would
     *  currently be a no-op, but relying on that would be fragile). */
    bool moveKeyboardFocus(int delta);

    /** Left/Right on a focused section (Sub)Header folds/expands it via the SAME
     *  setSectionCollapsed() a mouse click on its chevron already calls — no parallel mechanism.
     *  LOCKED decision (T160 scope): a no-op, not a bubble-worthy miss, when the focused row is
     *  anything else ("a focused child row"), so Left/Right never surprises the user by doing
     *  nothing visible AND letting the key leak to some unrelated global binding. */
    bool handleFoldKey(bool collapse);

    /** Enter-to-insert: activates whichever row is currently keyboard-focused via the same
     *  activateRow() the mouse path (mouseUp) already calls — see onModuleActivated/
     *  onSnippetActivated for what is genuinely NEW behaviour here versus what activateRow already
     *  did for Action/Plugin rows. */
    bool handleEnterKey();

    /** Scrolls just enough to bring the keyboard-focused row fully into the viewport below the
     *  pinned chrome — this sidebar is hand-scrolled (scrollOffset + a juce::ScrollBar), not a real
     *  juce::Viewport, so unlike T161's track headers there is no free auto-scroll here; arrow
     *  navigation has to drive it explicitly or it walks focus off screen with nothing visible
     *  moving. No-op if the row is already fully visible. */
    void scrollKeyboardFocusIntoView();

public:
    // -------------------------------------------------------------------------
    // Test / inspection helpers
    // -------------------------------------------------------------------------

    /** Every draggable (non-header) entry, in display order — exactly the strings this component puts
     *  in the drag payload. Tests use this so a module added here is automatically covered instead of
     *  needing a parallel hand-kept list. */
    /** Names of the module TYPES the library offers — i.e. every row that maps to a factory entry.
     *  Filtered on RowKind::Module rather than "not a header": the sidebar also carries snippet rows
     *  and the "No snippets yet" placeholder, and neither is a module type callers can instantiate. */
    /** True when a press on a row must NOT start (or activate) a drag — a context-menu press.
     *
     *  TRUE right button only, deliberately not isPopupMenu(): on macOS JUCE defines
     *  popupMenuClickModifier as (rightButtonModifier | ctrlModifier), so isPopupMenu() is also true
     *  for Ctrl+LEFT-click. Ctrl is the insert-between drag modifier, so testing isPopupMenu() here
     *  meant a Ctrl-held press on a library row never started a drag and Ctrl+drag-from-library
     *  could not reach the canvas at all. Extracted so the rule is testable without a live
     *  DragAndDropContainer. */
    static bool pressSuppressesRowDrag(const juce::ModifierKeys& mods) { return mods.isRightButtonDown(); }

    juce::StringArray getDraggableModuleNames() const;

    /** The section header a draggable entry sits under ("Sources", "Time FX", …), or an empty
     *  string when `moduleName` is not in the library. Tests use this to assert that other
     *  per-module groupings — cable colour categories, for one — stay in sync with the library's
     *  own sections instead of drifting behind a hand-kept copy of this list. */
    juce::String getSectionForModule(const juce::String& moduleName) const;

    /** Returns the currently hovered entry index, or -1 when nothing is hovered. */
    int getHoveredIndex() const noexcept { return hoveredIndex; }

    /** T160: the entry index Up/Down keyboard navigation has landed on, or -1. */
    int getKeyboardFocusedIndex() const noexcept { return keyboardFocusedIndex; }

    /** T160 test seam: drives the same state moveKeyboardFocus()/keyPressed() would, without a
     *  real native peer — see FocusRegion.h's own comment on why this test suite can never create
     *  one for grabKeyboardFocus() to require. Clamped exactly like a real navigation move so a test
     *  can't put the component into a state real navigation never could. */
    void setKeyboardFocusedIndexForTest(int index);

    /** T160 test seam: drives the private KeyListener::keyPressed(key, &searchEditor) overload a
     *  real native peer would invoke while the search field has focus — see the class comment on
     *  the keyboard-navigation section for why that path can't be reached by giving searchEditor
     *  real focus and calling its own keyPressed() headlessly (that call would exercise
     *  juce::TextEditor's own key handling, not the interception this seam targets). */
    bool simulateSearchFieldKeyPressForTest(const juce::KeyPress& key) { return keyPressed(key, &searchEditor); }

    /** Total number of entries (headers + items), collapsed or not. */
    int getEntryCount() const noexcept { return (int)entries.size(); }

    /** Display text of the entry at `index`, or an empty string when out of range. */
    juce::String getEntryText(int index) const;

    const Entry& getEntry(int index) const { return entries[(size_t)index]; }

    /** Number of rows currently drawn — shrinks as sections collapse. */
    int getVisibleRowCount() const { return (int)buildRows().size(); }

    /** Index of the first draggable row, or -1. Lets callers locate a row without hard-coding a
     *  y-offset that shifts every time the sidebar gains a section. */
    int getFirstDraggableEntryIndex() const;

    /** Vertical centre of an entry's row, or -1 when the entry is not currently visible. */
    int getRowCentreY(int entryIndex) const;

    /** Returns the entry index whose visible row contains contentY, or -1 if none.
     *  Takes a *content-space* y — the same space buildRows() and getRowCentreY() report, which is
     *  component space only while the panel is scrolled to the top. Mouse handlers go through
     *  getEntryIndexAtComponentY() instead. */
    int getEntryIndexAt(int contentY) const;

    /** Component-space y → entry index, or -1. The search field and collapse strip are pinned
     *  chrome, so a row scrolled underneath them is never a hit. */
    int getEntryIndexAtComponentY(int y) const;

    /** Bounds (component space) of the small "?" help button on the collapse-all strip, left of
     *  the COLLAPSE ALL / EXPAND ALL label. paint() and the mouse handlers share this one rect so
     *  the drawn button and the clickable button can never drift apart. */
    static juce::Rectangle<int> getHelpButtonBounds() noexcept;

    bool isHelpButtonHoveredForTest() const noexcept { return helpButtonHovered; }

    // -------------------------------------------------------------------------
    // Help popover pin/float test seams (round 2) — see the class comment on
    // synth::ui::ModuleLibraryHelpPopup for why the CallOutBox/floating split is implemented the
    // way it is. Every seam below is safe to call on a plain ModuleLibraryComponent EXCEPT where
    // noted: pinning never launches a real CallOutBox, only UN-pinning (or opening while unpinned)
    // does, which is why those two are reached through the launchHelpCallOutBox() virtual instead.
    // -------------------------------------------------------------------------

    /** Returns the persistent help popup content, creating it on first use — the same object
     *  showHelpPopover() shows, whichever host it currently lives in (or none, before the first
     *  open). Never launches a juce::CallOutBox or creates a floating window by itself. */
    synth::ui::ModuleLibraryHelpPopup* createHelpPopupForTest();

    /** Re-generates the "Key shortcuts" section against the currently-wired ShortcutManager,
     *  exactly as showHelpPopover() does on every real open — see
     *  ModuleLibraryHelpPopup::refreshShortcutSection for why a persistent instance needs this. */
    void refreshHelpPopoverForTest();

    bool isHelpPopoverPinnedForTest() const noexcept { return helpPopup_ && helpPopup_->isPinned(); }

    /** Drives the SAME pin transition the popover's own pin icon requests. Pinning itself never
     *  constructs a juce::CallOutBox (only un-pinning does, via launchHelpCallOutBox()), so this
     *  is safe to call with `true` on a plain ModuleLibraryComponent in a headless test; calling
     *  it with `false` needs launchHelpCallOutBox() stubbed first (see
     *  RecordingCallOutBoxModuleLibraryComponent in ModuleLibraryHelpPopupTests.cpp). */
    void setHelpPopoverPinnedForTest(bool pinned) { setHelpPopoverPinned(pinned); }

    /** Drives the SAME close path the popover's own close (X) requests. Never touches
     *  launchHelpCallOutBox() regardless of prior pin state, so always safe headlessly. */
    void closeHelpPopoverForTest() { closeHelpPopover(); }

protected:
    /** Ensures the persistent popup exists, refreshes its live-bound content, and shows it through
     *  whichever host its current pin state calls for. This is the ONE entry point mouseDown()
     *  routes the help button's click through; it never constructs a juce::CallOutBox directly
     *  (see launchHelpCallOutBox() below), so overriding just that leaf lets a test exercise this
     *  method's real pin-aware dispatch without ever creating a real top-level window — the same
     *  "protected virtual leaf" seam idiom docs/timeline/ruler.md#opening-a-menu-is-a-protected-virtual
     *  documents for TimelineRulerComponent::openMarkerContextMenu. */
    virtual void showHelpPopover();

    /** Constructs the actual juce::CallOutBox around the persistent popup — the one real
     *  top-level-window-creating leaf in this class (see the class comment on
     *  synth::ui::ModuleLibraryHelpPopup for why launchAsynchronously is deliberately NOT used
     *  here). A test overrides just this to stub the window while leaving showHelpPopover()'s and
     *  setHelpPopoverPinned()'s real dispatch/re-hosting logic intact. */
    virtual void launchHelpCallOutBox();

private:
    void ensureHelpPopupCreated();

    /** The ancestor a pinned popup floats in: walked all the way to the root of whatever window
     *  this component is currently inside, so the popup can extend beyond the sidebar's own narrow
     *  bounds over the canvas ("owned by the library/main UI", never a new desktop window). Falls
     *  back to `from` itself when there is no parent yet (every headless test, and the brief window
     *  before this component is added to the real app) — degraded but harmless, since nothing
     *  about the transition itself depends on which component ends up hosting it. */
    static juce::Component* floatingHelpHostFor(juce::Component& from);

    /** Clamps `desired` so `popup`'s ENTIRE rect — header included, since that is what sits at the
     *  popup's own local (0,0) — stays within `host`'s local bounds. This is the fix for a real
     *  bug: the previous pin transition placed the popup at "wherever the callout happened to be
     *  on screen", and the callout is anchored on the help button, which lives in the sidebar's
     *  OWN topmost strip — very close to the top of the whole window. Any small mismatch between
     *  the callout's screen position and `host`'s (a border inset, a display-scaling rounding, a
     *  host whose own top-left is not screen (0,0)) landed the popup with a slightly negative Y,
     *  which pushes the header — drawn at the popup's own y≈kOuterPadding — above y=0 of the host
     *  and off screen entirely, while the viewport content below it (larger local y) stayed
     *  visible and scrollable. That exactly matches the reported symptom: header gone, content
     *  starting mid-sentence, nothing left to click for move/close. Never trust an unclamped
     *  position for a component the user must always be able to reach again. */
    static juce::Point<int> clampToHost(const juce::Component& host, const juce::Component& popup,
                                        juce::Point<int> desired);

    /** Where a freshly-pinned popup appears: just to the right of the library sidebar, level with
     *  its top — a sensible, predictable spot a first-time user is already looking near, clear of
     *  any toolbar above the sidebar — rather than reusing the callout's screen position (see
     *  clampToHost() for why that was fragile). Always clamped, so this is safe even when the host
     *  is smaller than the sidebar's own width plus the popup (the popup then simply pins to the
     *  host's top-left, which keeps the header reachable even though it can no longer sit "beside"
     *  the sidebar). */
    juce::Point<int> defaultFloatingPosition(juce::Component& host) const;

    /** Keeps a pinned popover's header reachable across a host resize (the sidebar resizing is the
     *  best proxy this component has for "the window/host may have changed size" — see resized()).
     *  No-op unless the popup exists, is pinned, and is currently parented somewhere. */
    void reclampFloatingHelpPopover();

    /** Re-hosts the SAME persistent popup between a juce::CallOutBox (unpinned) and a plain,
     *  non-modal floating child of floatingHelpHostFor() (pinned) — see the class comment on
     *  synth::ui::ModuleLibraryHelpPopup for why this transplant is only safe because
     *  launchAsynchronously is never used to show it. No-op if the popup does not exist yet or is
     *  already in the requested state. */
    void setHelpPopoverPinned(bool wantPinned);

    /** Hides and detaches the popup from whichever host currently shows it, and resets pin state
     *  so the next "?" click (or setHelpPopoverPinned(true)) starts clean. The popup object itself
     *  survives — closing is not the same as never having opened it. */
    void closeHelpPopover();

    float targetProgressFor(const juce::String& header) const { return isSectionCollapsed(header) ? 1.0f : 0.0f; }

    void snapSectionProgressToTargets();

    /** Tweens every section from where it is now to where the logical state says it should be.
     *  One driver covers all sections so "collapse all" folds them together rather than firing
     *  nine competing animations. */
    void startCollapseAnimation();

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;

    /** Slim themed width (AppLookAndFeel::kScrollbarWidth), or the JUCE default headlessly. */
    int getScrollBarWidth() const { return juce::jmax(4, getLookAndFeel().getDefaultScrollbarWidth()); }

    int getRowContentWidth() const;

    /** Shows/hides and re-ranges the scrollbar for the current row set, and re-clamps the offset.
     *  Must run after anything that changes the content height — a resize, a collapse, a search
     *  filter, or a snippet refresh — or a shrinking list would leave the view scrolled past its
     *  own end. */
    void updateScrollBar();

    bool isDraggableEntry(int index) const;

    bool isActionEntry(int index) const;

    /** Draggable rows plus the command rows — everything that highlights on hover. */
    bool isInteractiveEntry(int index) const { return isDraggableEntry(index) || isActionEntry(index); }

    bool isHeaderEntry(int index) const;

    bool isSubHeaderEntry(int index) const;

    static synth::PluginIdentity identityForEntry(const Entry& entry);

    juce::String tooltipForEntry(int index) const;

    /** Starts the DragAndDrop session for a draggable row. Every payload rides the same channel and
     *  is told apart by its prefix — plain text is a module type, "snippet:" a saved group,
     *  "plugin:" a scanned plugin identity. */
    void startDragForEntry(int index);

    /** Drops a hover that a collapse (or a snippet-list refresh) just hid, so no highlight is
     *  painted for a row that is no longer on screen. */
    void clampHoverToVisibleRow();

    /** T160 sibling of clampHoverToVisibleRow() above, for keyboardFocusedIndex — called from every
     *  site that calls that one, so a snippet save, plugin scan, search-query change, or collapse
     *  animation finishing can never leave keyboard focus parked on a row that just left the visible
     *  set (Enter-to-insert on a hidden entry would otherwise silently activate the wrong module). */
    void clampKeyboardFocusToVisibleRow();

    void applySearchQuery(const juce::String& text);

    bool sectionVisibleInSearch(size_t headerIndex, size_t end) const;

    bool childVisibleInSearch(const Entry& entry) const;

    void applySearchEditorColours();

    static void drawHighlightedText(juce::Graphics& g, const juce::String& text, const juce::String& query,
                                    juce::Rectangle<int> bounds, const juce::Font& font, juce::Colour normal,
                                    juce::Colour highlightFill, juce::Colour highlightText);

    /** @param progress 0 = open (pointing down) .. 1 = folded (pointing right). Drawn as the open
     *  triangle rotated by -90° * progress: for a square area the endpoints are exactly the two
     *  shapes this used to switch between, so 0 and 1 look identical to the old two-state version
     *  while everything in between is a real rotation. */
    static void drawChevron(juce::Graphics& g, juce::Rectangle<float> area, float progress, juce::Colour colour);

public:
    /** Maps a category header string to its Icon enum value. Public (rather than the private
     *  section every other static paint helper here lives in) so IconLibrary/ModuleLibrary tests
     *  can assert the mapping directly instead of rendering a row and inspecting pixels. */
    static synth::theme::Icon categoryIconForHeader(const juce::String& header);

private:
    /** Rebuilds the flat entry list: the Snippets section first (it holds what the user just made
     *  and reaches for most), then the fixed module catalogue. */
    void rebuildEntries();

    std::vector<Entry> entries;
    juce::Array<synth::SnippetInfo> snippets;
    std::vector<synth::PluginIdentity> plugins;
    std::set<juce::String> collapsedSections;
    int hoveredIndex = -1; // -1 = no hover; updated on mouseMove/mouseExit only
    // T160: -1 = nothing keyboard-focused. Mirrors hoveredIndex's shape but is driven entirely by
    // keyPressed()/the searchEditor KeyListener, never by the mouse — the two are independent
    // visual states (see paint()'s separate outline for this one). Clamped at every site that
    // clamps hoveredIndex (clampHoverToVisibleRow's call sites) so a snippet save, plugin scan, or
    // collapse animation completing can never leave it pointing at a row that is no longer visible.
    int keyboardFocusedIndex = -1;
    int pressedIndex = -1;          // row whose click is pending a mouseUp (Action / Plugin rows only)
    bool topStripHovered = false;   // hover state for the collapse-all chrome
    bool helpButtonHovered = false; // hover state for the "?" help button sharing that row
    const ShortcutManager* shortcutManager = nullptr; // read-only; see setShortcutManager()

    /** Pixels of movement that turn a plugin-row press into a drag rather than a click. */
    static constexpr int kDragStartThresholdPx = 4;

    juce::TextEditor searchEditor;
    juce::String searchQuery; // raw editor text; isSearchActive() trims it

    juce::ScrollBar verticalScrollBar{true};
    int scrollOffset = 0; // px of content scrolled past the top of the row viewport

    // Per-section fold amount, 0 = open .. 1 = closed. Purely visual; the logical state is
    // `collapsedSections`. Created on demand so a headless component never builds a VBlank
    // attachment it cannot use.
    std::map<juce::String, float> sectionProgress;
    std::optional<juce::VBlankAnimatorUpdater> vblankUpdater;
    synth::ui::AnimationDriver collapseAnim;

    // Help popover (round 2: pin/float) — helpPopup_ is the ONE persistent content instance a pin
    // click re-hosts; helpCallOutBox_ exists only while it is shown unpinned. Declared in THIS
    // order (helpPopup_ first) so automatic member teardown destroys helpCallOutBox_ FIRST
    // (reverse declaration order) — it holds a non-owning `Component&` to *helpPopup_, so tearing
    // it down after would leave a dangling reference for the instant before its own destructor ran.
    std::unique_ptr<synth::ui::ModuleLibraryHelpPopup> helpPopup_;
    std::unique_ptr<juce::CallOutBox> helpCallOutBox_;
};
