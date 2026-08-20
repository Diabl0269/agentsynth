#pragma once

#include "../Plugin/Hosting/HostedPluginBackend.h"
#include "../SnippetManager.h"
#include "Theme/AppLookAndFeel.h"
#include "UIAnimation.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <set>
#include <vector>

class ModuleLibraryComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::SettableTooltipClient
    , private juce::ScrollBar::Listener {
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

    ModuleLibraryComponent() {
        // The flat entry list is rebuilt rather than assigned literally, because it now has to be
        // regenerated whenever the snippet list changes. The module catalogue itself lives in
        // rebuildEntries() — add new modules there.
        rebuildEntries();
        snapSectionProgressToTargets();
        setMouseCursor(juce::MouseCursor::NormalCursor);
        // Prevent the parent component from grabbing keyboard focus when clicked (e.g. on the
        // collapse-all strip). Without this, clicking anywhere in the parent would cause the
        // searchEditor child to gain focus, clearing its placeholder text.
        setMouseClickGrabsKeyboardFocus(false);

        // addChildComponent, not addAndMakeVisible: updateScrollBar() owns the visibility, so the bar
        // only appears once the rows actually outgrow the panel.
        addChildComponent(verticalScrollBar);
        verticalScrollBar.setAutoHide(false);
        verticalScrollBar.addListener(this);

        searchEditor.setMultiLine(false);
        searchEditor.setReturnKeyStartsNewLine(false);
        searchEditor.setEscapeAndReturnKeysConsumed(true);
        searchEditor.setSelectAllWhenFocused(true);
        searchEditor.setJustification(juce::Justification::centredLeft);
        searchEditor.setBorder(juce::BorderSize<int>(0));
        searchEditor.setIndents(6, 0);
        searchEditor.setFont(juce::Font(juce::FontOptions(13.0f)));
        searchEditor.setTooltip("Filter the library by module, snippet, or category name.");
        searchEditor.onTextChange = [this] { applySearchQuery(searchEditor.getText()); };
        searchEditor.onEscapeKey = [this] {
            if (searchEditor.getText().isNotEmpty())
                setSearchText({});
        };
        addAndMakeVisible(searchEditor);
        applySearchEditorColours();
    }

    ~ModuleLibraryComponent() override {
        // The animator's callbacks capture `this`, so it must not outlive us.
        if (vblankUpdater.has_value())
            collapseAnim.stop(*vblankUpdater);
        verticalScrollBar.removeListener(this);
    }

    // -------------------------------------------------------------------------
    // Snippets (issue #156)
    // -------------------------------------------------------------------------

    /** Replaces the Snippets section contents. Called by the owner after any snippet is saved or
     *  deleted; the sidebar itself never touches the filesystem. */
    void setSnippets(const juce::Array<synth::SnippetInfo>& newSnippets) {
        snippets.clearQuick();
        snippets.addArray(newSnippets);
        rebuildEntries();
        clampHoverToVisibleRow();
        updateScrollBar();
        repaint();
    }

    int getSnippetCount() const noexcept { return snippets.size(); }

    /** Invoked when the user picks "Delete Snippet" from a snippet row's context menu. */
    std::function<void(const juce::String&)> onSnippetDeleteRequested;

    // -------------------------------------------------------------------------
    // Search
    // -------------------------------------------------------------------------

    /** Trimmed query: empty means the library is unfiltered. */
    static juce::String normalisedSearchQuery(const juce::String& raw) { return raw.trim(); }

    static bool textMatchesQuery(const juce::String& text, const juce::String& query) {
        const auto q = normalisedSearchQuery(query);
        return q.isNotEmpty() && text.containsIgnoreCase(q);
    }

    /** Non-overlapping case-insensitive hits of `query` inside `text`, in left-to-right order. */
    static std::vector<HighlightSpan> highlightSpansFor(const juce::String& text, const juce::String& query) {
        std::vector<HighlightSpan> spans;
        const auto q = normalisedSearchQuery(query);
        if (q.isEmpty() || text.isEmpty())
            return spans;
        const int qLen = q.length();
        int from = 0;
        while (from + qLen <= text.length()) {
            const int hit = text.indexOfIgnoreCase(from, q);
            if (hit < 0)
                break;
            spans.push_back({hit, qLen});
            from = hit + qLen;
        }
        return spans;
    }

    void setSearchText(const juce::String& text) {
        if (searchEditor.getText() != text)
            searchEditor.setText(text, juce::dontSendNotification);
        applySearchQuery(text);
    }

    juce::String getSearchText() const { return searchEditor.getText(); }

    bool isSearchActive() const { return normalisedSearchQuery(searchQuery).isNotEmpty(); }

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
    void setPlugins(const std::vector<synth::PluginIdentity>& newPlugins) {
        plugins = newPlugins;
        rebuildEntries();
        clampHoverToVisibleRow();
        updateScrollBar();
        repaint();
    }

    int getPluginCount() const noexcept { return (int)plugins.size(); }

    /** Fired when the user clicks the "Scan for plugins..." row. */
    std::function<void()> onScanPluginsRequested;

    /** Fired when the user clicks (rather than drags) a plugin row — the owner adds the module at a
     *  sensible canvas position. Dragging goes through the DragAndDrop payload instead. */
    std::function<void(const synth::PluginIdentity&)> onPluginActivated;

    /** Performs the click action for the row at `index`: fires the scan request for the Action row,
     *  or onPluginActivated for a Plugin row. No-op for anything else. Public so the behaviour is
     *  reachable without synthesising mouse events. */
    void activateRow(int index) {
        if (index < 0 || index >= (int)entries.size())
            return;

        const auto& entry = entries[(size_t)index];
        if (entry.kind == RowKind::Action) {
            if (onScanPluginsRequested)
                onScanPluginsRequested();
            return;
        }
        if (entry.kind == RowKind::Plugin && onPluginActivated)
            onPluginActivated(identityForEntry(entry));
    }

    /** The identity a Plugin row stands for; an invalid identity for any other row. */
    synth::PluginIdentity getPluginIdentity(int index) const {
        if (index < 0 || index >= (int)entries.size() || entries[(size_t)index].kind != RowKind::Plugin)
            return {};
        return identityForEntry(entries[(size_t)index]);
    }

    // -------------------------------------------------------------------------
    // Collapse / expand
    // -------------------------------------------------------------------------

    bool isSectionCollapsed(const juce::String& header) const {
        return collapsedSections.find(header) != collapsedSections.end();
    }

    void setSectionCollapsed(const juce::String& header, bool collapsed) {
        const bool changed =
            collapsed ? collapsedSections.insert(header).second : (collapsedSections.erase(header) > 0);
        if (!changed)
            return;
        startCollapseAnimation();
        if (onCollapseStateChanged)
            onCollapseStateChanged();
        repaint();
    }

    void toggleSection(const juce::String& header) { setSectionCollapsed(header, !isSectionCollapsed(header)); }

    /** True when every section is collapsed — drives the top strip's label and its action. */
    bool areAllSectionsCollapsed() const {
        for (const auto& entry : entries)
            if (entry.kind == RowKind::Header && !isSectionCollapsed(entry.text))
                return false;
        return true;
    }

    /** Collapses every section, or expands every section when they are already all collapsed. */
    void toggleAllSections() { setAllSectionsCollapsed(!areAllSectionsCollapsed()); }

    void setAllSectionsCollapsed(bool collapsed) {
        std::set<juce::String> next;
        if (collapsed) {
            for (const auto& entry : entries)
                if (entry.kind == RowKind::Header)
                    next.insert(entry.text);
        }
        if (next == collapsedSections)
            return;
        collapsedSections = std::move(next);
        startCollapseAnimation();
        if (onCollapseStateChanged)
            onCollapseStateChanged();
        repaint();
    }

    /** Collapsed section names, for persistence by the owner. */
    juce::StringArray getCollapsedSections() const {
        juce::StringArray result;
        for (const auto& header : collapsedSections)
            result.add(header);
        return result;
    }

    /** Restores a persisted collapse state. Does NOT fire onCollapseStateChanged — this IS the
     *  restore path, and re-notifying would write back what we just read. */
    void setCollapsedSections(const juce::StringArray& headers) {
        collapsedSections.clear();
        for (const auto& header : headers) {
            // Persisted state arrives as newline-joined text, so an empty setting yields one blank
            // entry — never store it, or areAllSectionsCollapsed() counts a section that isn't real.
            if (header.isNotEmpty())
                collapsedSections.insert(header);
        }
        // Restore path — snap, never animate: the user did not fold anything, and animating on
        // launch would look like the sidebar collapsing by itself.
        snapSectionProgressToTargets();
        clampHoverToVisibleRow();
        updateScrollBar();
        repaint();
    }

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
    float getSectionProgress(const juce::String& header) const {
        const auto it = sectionProgress.find(header);
        return it != sectionProgress.end() ? it->second : targetProgressFor(header);
    }

    /** Sets the visual fold amount directly, without touching the logical collapse state.
     *  Normally the animation owns this; it is exposed so the accordion geometry can be exercised
     *  at intermediate values, which a VBlank-driven clock cannot produce headlessly. */
    void setSectionProgress(const juce::String& header, float progress) {
        sectionProgress[header] = juce::jlimit(0.0f, 1.0f, progress);
        updateScrollBar();
        repaint();
    }

    bool isCollapseAnimating() const noexcept { return collapseAnim.isRunning(); }

    /** Drops any in-flight animation onto its final layout. */
    void finishCollapseAnimation() {
        if (vblankUpdater.has_value())
            collapseAnim.stop(*vblankUpdater);
        snapSectionProgressToTargets();
        clampHoverToVisibleRow();
        updateScrollBar();
        repaint();
    }

    /** Optional predicate deciding whether a module can currently be added. Used for the singleton
     *  I/O modules: once a patch has an Audio Output, its row greys out and stops being draggable,
     *  rather than accepting a drag that would silently do nothing. Unset means everything is
     *  available, which keeps headless tests and every non-singleton module unaffected. */
    std::function<bool(const juce::String&)> isModuleAvailable;

    /** True when the row at `index` is a draggable row that can currently be added. Snippet rows are
     *  draggable but never gated — the predicate only ever describes module types. */
    bool isEntryEnabled(int index) const {
        if (!isDraggableEntry(index))
            return false;
        if (entries[(size_t)index].kind != RowKind::Module)
            return true;
        return !isModuleAvailable || isModuleAvailable(entries[(size_t)index].text);
    }

    // -------------------------------------------------------------------------
    // Pure static helpers — callable headlessly (no GUI / MessageManager needed)
    // -------------------------------------------------------------------------

    /** Returns a one-line description for a known module name, or a generic
     *  fallback string for unknown names. */
    static juce::String descriptionFor(const juce::String& moduleName) {
        if (moduleName.equalsIgnoreCase("Oscillator"))
            return "Generates audio waveforms (sine, saw, square, triangle). Switch Poly on to run "
                   "8 voices driven by a Poly MIDI pitch fan.";
        if (moduleName.equalsIgnoreCase("Wavetable"))
            return "Scans through 3D wavetables — six built-ins or load your own file.";
        if (moduleName.equalsIgnoreCase("Noise"))
            return "Generates noise (white, pink, brown).";
        if (moduleName.equalsIgnoreCase("Sampler"))
            return "Plays an audio file back as a sample or scatters it into grains.";
        if (moduleName.equalsIgnoreCase("LFO"))
            return "Low-frequency oscillator for slow cyclic modulation.";
        if (moduleName.equalsIgnoreCase("Sequencer"))
            return "Step sequencer that outputs pitch and gate CV signals.";
        if (moduleName.equalsIgnoreCase("Poly Sequencer"))
            return "Polyphonic step sequencer for multi-voice melodies.";
        if (moduleName.equalsIgnoreCase("MidiKeyboard"))
            return "On-screen MIDI keyboard for note input.";
        if (moduleName.equalsIgnoreCase("Poly MIDI"))
            return "Converts MIDI into 8 voices of pitch and gate CV. Patch Poly Out to an "
                   "Oscillator's Pitch and an ADSR's Gate, and switch Poly on for every module in "
                   "the chain (Oscillator, ADSR, Filter, VCA) — with Poly off, only one voice sounds.";
        if (moduleName.equalsIgnoreCase("External MIDI"))
            return "Routes external MIDI device input into the patch graph.";
        if (moduleName.equalsIgnoreCase("ADSR"))
            return "Attack-Decay-Sustain-Release envelope generator. Gate CV or MIDI starts the "
                   "envelope; Threshold sets how high the gate must rise. Switch Poly on for one "
                   "envelope per voice.";
        if (moduleName.equalsIgnoreCase("Envelope Follower"))
            return "Tracks an audio signal's amplitude and outputs it as modulation CV.";
        if (moduleName.equalsIgnoreCase("VCA"))
            return "Voltage-controlled amplifier — controls signal amplitude via CV. Switch Poly on "
                   "to gain-control 8 voices and sum them to stereo.";
        if (moduleName.equalsIgnoreCase("Filter"))
            return "Multi-mode resonant filter (low-pass, high-pass, band-pass). Switch Poly on to "
                   "filter 8 voices; cutoff and resonance CV stay shared across them.";
        if (moduleName.equalsIgnoreCase("Parametric EQ"))
            return "Four-band EQ with a visual response curve for surgical tone shaping.";
        if (moduleName.equalsIgnoreCase("Chorus"))
            return "Adds lush width by layering slightly detuned copies of the signal.";
        if (moduleName.equalsIgnoreCase("Phaser"))
            return "Sweeping all-pass phase modulation effect.";
        if (moduleName.equalsIgnoreCase("Flanger"))
            return "Short delay feedback comb-filter with a sweeping metallic sound.";
        if (moduleName.equalsIgnoreCase("Distortion"))
            return "Waveshaping distortion from soft saturation to hard clipping.";
        if (moduleName.equalsIgnoreCase("Ring Modulator"))
            return "Oversampled diode-ring modulator — metallic, bell-like sum and difference tones.";
        if (moduleName.equalsIgnoreCase("Bitcrusher"))
            return "For Lo-Fi, sample-rate reduction, and retro digital grit.";
        if (moduleName.equalsIgnoreCase("Pitch Shifter"))
            return "Transposes by semitones or shifts every partial by a fixed number of Hz.";
        if (moduleName.equalsIgnoreCase("Delay"))
            return "Tempo-syncable stereo echo / delay line.";
        if (moduleName.equalsIgnoreCase("Reverb"))
            return "Algorithmic reverb for adding space and depth.";
        if (moduleName.equalsIgnoreCase("Compressor"))
            return "Dynamic range compressor with threshold, ratio, attack and release.";
        if (moduleName.equalsIgnoreCase("Limiter"))
            return "Brickwall limiter that prevents the signal from exceeding 0 dBFS.";
        if (moduleName.equalsIgnoreCase("Macros"))
            return "Bank of assignable macro knobs — one knob drives many parameters at once.";
        if (moduleName.equalsIgnoreCase("Sample & Hold"))
            return "Latches a source value on each clock edge for stepped random CV.";
        if (moduleName.equalsIgnoreCase("Voice Mixer"))
            return "Sums multiple polyphonic voices down to a stereo mix.";
        if (moduleName.equalsIgnoreCase("Math"))
            return "Dual-input math/logic utility - Sum, Difference, Min, Max and Product of A and B.";
        if (moduleName.equalsIgnoreCase("Comparator"))
            return "Emits a gate while the Signal is above Threshold, plus the inverted gate. Slice "
                   "an LFO, a kick, or any CV into a pulse.";
        if (moduleName.equalsIgnoreCase("Audio Input"))
            return "Audio from the input device — one jack per input channel. Only one per patch.";
        if (moduleName.equalsIgnoreCase("Audio Output"))
            return "Sends the patch to the output device. Only one per patch.";
        // Generic fallback for any unrecognised module name.
        return "Audio processing module.";
    }

    /** Tooltip for a saved snippet row. */
    static juce::String snippetDescription(const juce::String& name, int moduleCount) {
        return "Snippet \"" + name + "\" — " + juce::String(moduleCount) +
               (moduleCount == 1 ? " module. " : " modules. ") + "Drag onto the canvas to insert the whole group.";
    }

    /** Tooltip for a scanned plugin row. */
    static juce::String pluginDescription(const juce::String& name, const juce::String& format) {
        return name + " (" + format +
               ") — a plugin installed on this machine. Drag it onto the canvas, or click "
               "to drop it in the middle.";
    }

    /** Tooltip for the scan row. */
    static juce::String scanRowDescription(bool anyPluginsKnown) {
        return anyPluginsKnown ? "Rescan for installed plugins. Each one is checked in its own process, so a plugin "
                                 "that crashes cannot take the app down."
                               : "Look for VST3 and Audio Unit plugins installed on this machine. Each one is "
                                 "checked in its own process, so a plugin that crashes cannot take the app down.";
    }

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
    std::vector<Row> buildRows() const {
        std::vector<Row> rows;
        int y = kPinnedChromeHeight + kFirstRowY;
        bool seenHeader = false;
        const bool filtering = isSearchActive();

        size_t i = 0;
        while (i < entries.size()) {
            if (entries[i].kind != RowKind::Header) {
                ++i; // defensive: today every row follows a header
                continue;
            }

            // Span of rows belonging to this header.
            size_t end = i + 1;
            while (end < entries.size() && entries[end].kind != RowKind::Header)
                ++end;

            std::vector<size_t> visibleChildren;
            if (filtering) {
                if (!sectionVisibleInSearch(i, end)) {
                    i = end;
                    continue;
                }
                for (size_t j = i + 1; j < end; ++j)
                    if (childVisibleInSearch(entries[j]))
                        visibleChildren.push_back(j);
            } else {
                for (size_t j = i + 1; j < end; ++j)
                    visibleChildren.push_back(j);
            }

            if (seenHeader)
                y += kHeaderGap;
            seenHeader = true;
            rows.push_back({(int)i, y, kHeaderHeight});
            y += kHeaderHeight;

            const int naturalHeight = (int)visibleChildren.size() * kItemHeight;
            // Search forces matching sections open without touching collapse progress, so typing
            // does not fire the accordion (or persist a fold the user never asked for).
            const float progress = filtering ? 0.0f : getSectionProgress(entries[i].text);
            const int bandHeight = juce::roundToInt((float)naturalHeight * (1.0f - progress));
            const int bandTop = y;

            for (size_t c = 0; c < visibleChildren.size(); ++c) {
                const int rowTop = bandTop + (int)c * kItemHeight;
                const int visibleHeight = juce::jlimit(0, kItemHeight, bandTop + bandHeight - rowTop);
                if (visibleHeight > 0)
                    rows.push_back({(int)visibleChildren[c], rowTop, visibleHeight});
            }

            y = bandTop + bandHeight;
            i = end;
        }
        return rows;
    }

    /** Total pixel height of the currently visible content. */
    int getTotalContentHeight() const {
        auto rows = buildRows();
        return rows.empty() ? kPinnedChromeHeight + kFirstRowY : rows.back().y + rows.back().height + kFirstRowY;
    }

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
    bool setScrollOffset(int newOffset) {
        const int clamped = juce::jlimit(0, getMaxScrollOffset(), newOffset);
        if (clamped == scrollOffset)
            return false;
        scrollOffset = clamped;
        repaint();
        return true;
    }

    /** True when the rows overflow the panel and the scrollbar is therefore on screen. */
    bool isScrollBarVisible() const noexcept { return verticalScrollBar.isVisible(); }

    void resized() override {
        searchEditor.setBounds(8, 4, juce::jmax(0, getWidth() - 16), kSearchHeight - 8);
        updateScrollBar();
    }

    void lookAndFeelChanged() override {
        // Scrollbar width is a theme token (AppLookAndFeel::kScrollbarWidth), so a theme switch can
        // change the bar's footprint and the width left for row text.
        applySearchEditorColours();
        updateScrollBar();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        if (verticalScrollBar.isVisible())
            verticalScrollBar.mouseWheelMove(e.getEventRelativeTo(&verticalScrollBar), wheel);
        else
            juce::Component::mouseWheelMove(e, wheel);
    }

    // -------------------------------------------------------------------------
    // Paint
    // -------------------------------------------------------------------------

    void paint(juce::Graphics& g) override {
        // Resolve theme tokens from the active LnF; fall back to plain colors when our LnF
        // isn't installed (e.g. headless tests).
        juce::Colour bgColour = juce::Colours::darkgrey.darker();
        juce::Colour headerColour = juce::Colours::grey;
        juce::Colour itemColour = juce::Colours::white;
        juce::Colour accentColour = juce::Colours::lightblue;
        juce::Colour mutedColour = juce::Colours::grey;

        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        if (lf != nullptr) {
            const auto& c = lf->getTheme().colors;
            bgColour = c.bg0;
            headerColour = c.textMuted;
            itemColour = c.textPrimary;
            accentColour = c.accent;
            mutedColour = c.textMuted;
        }

        g.fillAll(bgColour);

        // Rows stop short of the scrollbar when it is on screen, so text never runs under the thumb.
        const int contentWidth = getRowContentWidth();
        const auto rows = buildRows();
        const juce::String query = normalisedSearchQuery(searchQuery);

        // ---- Rows: clipped below the pinned chrome and shifted by the scroll offset ----
        // The clip is what keeps a scrolled row from painting over the search field or the strip;
        // setOrigin then moves the content-space row.y values into component space.
        {
            juce::Graphics::ScopedSaveState scrolled(g);
            g.reduceClipRegion(0, kPinnedChromeHeight, getWidth(), juce::jmax(0, getHeight() - kPinnedChromeHeight));
            g.setOrigin(0, -scrollOffset);

            if (rows.empty() && isSearchActive()) {
                g.setColour(mutedColour);
                g.setFont(juce::Font(juce::FontOptions(13.0f)));
                g.drawText("No matching modules", 20, kPinnedChromeHeight + 12, contentWidth - 40, 24,
                           juce::Justification::centredLeft);
            }

            for (const auto& row : rows) {
                const auto& entry = entries[(size_t)row.entryIndex];

                if (entry.kind == RowKind::Header) {
                    // Disclosure chevron drawn as a path — glyph coverage for ▾/▸ is not guaranteed
                    // across the embedded typefaces (see the theming font limitation). It rotates on
                    // the same progress value as the fold, so the two read as one motion. Search
                    // forces matching sections open, so the chevron matches that layout.
                    const float chevronProgress = isSearchActive() ? 0.0f : getSectionProgress(entry.text);
                    drawChevron(g, juce::Rectangle<float>(8.0f, (float)row.y + 6.0f, 8.0f, 8.0f), chevronProgress,
                                headerColour);

                    // Category icon at x=20 (null-guarded — no-op when LnF absent).
                    synth::theme::Icon catIcon = categoryIconForHeader(entry.text);
                    const juce::Drawable* icon = (lf != nullptr) ? lf->peekIcon(catIcon) : nullptr;

                    const juce::Font headerFont(juce::FontOptions(12.0f));
                    const juce::String headerLabel = entry.text.toUpperCase();
                    if (icon != nullptr) {
                        icon->drawWithin(g, juce::Rectangle<float>(20.0f, (float)row.y + 2.0f, 16.0f, 16.0f),
                                         juce::RectanglePlacement::centred, 1.0f);
                        drawHighlightedText(g, headerLabel, query, {40, row.y, contentWidth - 50, 20}, headerFont,
                                            headerColour, accentColour.withAlpha(0.28f), accentColour);
                    } else {
                        drawHighlightedText(g, headerLabel, query, {20, row.y, contentWidth - 30, 20}, headerFont,
                                            headerColour, accentColour.withAlpha(0.28f), accentColour);
                    }
                    continue;
                }

                // A row mid-fold is truncated, not resized: clip to the visible slice and keep
                // drawing the text at its natural height, so it is cut off rather than squashed or
                // re-centred as the section closes. juce::Graphics::drawText does not clip on its
                // own, hence the explicit region.
                std::optional<juce::Graphics::ScopedSaveState> rowClip;
                if (row.height < kItemHeight) {
                    rowClip.emplace(g);
                    g.reduceClipRegion(0, row.y, getWidth(), row.height);
                }

                if (entry.kind == RowKind::EmptyHint) {
                    const juce::Font hintFont(juce::FontOptions(13.0f));
                    drawHighlightedText(g, entry.text, query, {20, row.y, contentWidth - 40, kItemHeight - 4}, hintFont,
                                        mutedColour.withAlpha(0.7f), accentColour.withAlpha(0.28f), accentColour);
                    continue;
                }

                // Sub-group label inside a section (e.g. "VST3" / "AudioUnit" under Plugins) — painted
                // in the same muted style as a Header but smaller and indented, so it reads as a
                // sub-level without competing with the section title. Non-clickable, no hover state.
                if (entry.kind == RowKind::SubHeader) {
                    g.setColour(mutedColour.withAlpha(0.6f));
                    g.setFont(juce::Font(juce::FontOptions(10.5f)));
                    g.drawText(entry.text.toUpperCase(), 28, row.y, contentWidth - 40, kItemHeight - 4,
                               juce::Justification::centredLeft);
                    continue;
                }

                // Command row — reads like a hint until hovered, so an empty Plugins section looks
                // like a prompt rather than like a broken module row.
                if (entry.kind == RowKind::Action) {
                    const bool hot = row.entryIndex == hoveredIndex;
                    if (hot) {
                        g.setColour(accentColour.withAlpha(0.12f));
                        g.fillRect(0, row.y, contentWidth, row.height);
                    }
                    g.setColour(hot ? accentColour : mutedColour.withAlpha(0.85f));
                    g.setFont(juce::Font(juce::FontOptions(13.0f)));
                    g.drawText(entry.text, 20, row.y, contentWidth - 40, kItemHeight - 4,
                               juce::Justification::centredLeft);
                    continue;
                }

                // Draggable row (module, snippet or plugin).
                const bool enabled = isEntryEnabled(row.entryIndex);

                if (row.entryIndex == hoveredIndex && enabled) {
                    g.setColour(accentColour.withAlpha(0.12f));
                    g.fillRect(0, row.y, contentWidth, row.height);
                }

                // Greyed out = already in the patch and not addable again.
                const juce::Colour labelColour = enabled ? itemColour : mutedColour.withAlpha(0.5f);
                const juce::Font itemFont(juce::FontOptions(16.0f));
                drawHighlightedText(g, entry.text, query, {20, row.y, contentWidth - 60, kItemHeight - 4}, itemFont,
                                    labelColour, accentColour.withAlpha(0.28f), accentColour);

                if (entry.kind == RowKind::Snippet) {
                    g.setColour(mutedColour);
                    g.setFont(juce::Font(juce::FontOptions(12.0f)));
                    g.drawText("(" + juce::String(entry.moduleCount) + ")", contentWidth - 44, row.y, 34,
                               kItemHeight - 4, juce::Justification::centredRight);
                } else if (entry.kind == RowKind::Plugin) {
                    // The format tag is load-bearing, not decoration: the same plugin often ships as
                    // both VST3 and AU, and the two are different entries with different state.
                    g.setColour(mutedColour);
                    g.setFont(juce::Font(juce::FontOptions(11.0f)));
                    g.drawText(entry.detail, contentWidth - 74, row.y, 64, kItemHeight - 4,
                               juce::Justification::centredRight);
                }
            }
        }

        // ---- Pinned chrome: the search field is a child TextEditor in the top 32 px; the
        // collapse-all strip is drawn here so scrolled rows cannot show through it. ----
        {
            const bool allCollapsed = areAllSectionsCollapsed();
            g.setColour(bgColour);
            g.fillRect(0, kSearchHeight, getWidth(), kTopStripHeight);
            g.setColour(topStripHovered ? accentColour : mutedColour);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(allCollapsed ? "EXPAND ALL" : "COLLAPSE ALL", 10, kSearchHeight + 2, contentWidth - 20,
                       kTopStripHeight - 4, juce::Justification::centredRight);
        }
    }

    // -------------------------------------------------------------------------
    // Mouse events
    // -------------------------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override {
        const bool wasTopStripHovered = topStripHovered;
        topStripHovered = isInTopStrip(e.y);

        const int entryUnderMouse = getEntryIndexAtComponentY(e.y);
        // Only interactive rows can be hovered; headers and hints clamp to -1.
        const int newIndex = isInteractiveEntry(entryUnderMouse) ? entryUnderMouse : -1;

        if (newIndex != hoveredIndex || topStripHovered != wasTopStripHovered) {
            hoveredIndex = newIndex;

            // Update tooltip: the shared TooltipWindow (owned by MainComponent) reads
            // this component's tooltip string on each hover. Setting it here on hover
            // change means each draggable row surfaces its per-module description.
            if (topStripHovered) {
                setTooltip("Collapse or expand every category in the library.");
            } else if (hoveredIndex >= 0) {
                setTooltip(tooltipForEntry(hoveredIndex));
            } else {
                setTooltip({});
            }

            repaint();
        }

        // Update cursor: grab hand for draggable items, pointing hand for the clickable chrome.
        // An unavailable row is not draggable, so it must not advertise the grab hand.
        if (hoveredIndex >= 0 && isDraggableEntry(hoveredIndex) && isEntryEnabled(hoveredIndex))
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else if (topStripHovered || isHeaderEntry(entryUnderMouse) || isActionEntry(hoveredIndex))
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredIndex != -1 || topStripHovered) {
            hoveredIndex = -1;
            topStripHovered = false;
            setTooltip({});
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        pressedIndex = -1;

        if (isInTopStrip(e.y)) {
            toggleAllSections();
            return;
        }

        const int index = getEntryIndexAtComponentY(e.y);
        if (index < 0 || index >= (int)entries.size())
            return;

        const auto& entry = entries[(size_t)index];

        if (entry.kind == RowKind::Header) {
            toggleSection(entry.text);
            return;
        }

        if (entry.kind == RowKind::EmptyHint || entry.kind == RowKind::SubHeader)
            return;

        // Click-activated rows (the scan command, and plugin rows, which support BOTH click-to-add
        // and drag-to-place) defer to mouseUp/mouseDrag. Module and snippet rows keep starting their
        // drag on mouse-down, which is what every existing drag test drives.
        if (entry.kind == RowKind::Action || entry.kind == RowKind::Plugin) {
            if (!e.mods.isPopupMenu())
                pressedIndex = index;
            return;
        }

        if (entry.kind == RowKind::Snippet && e.mods.isPopupMenu()) {
            const auto name = entry.text;
            juce::PopupMenu m;
            m.addItem("Delete Snippet", [this, name] {
                if (onSnippetDeleteRequested)
                    onSnippetDeleteRequested(name);
            });
            m.showMenuAsync(juce::PopupMenu::Options());
            return;
        }

        if (e.mods.isPopupMenu())
            return;

        // An unavailable row must not start a drag at all — accepting one and then dropping it on
        // the floor reads as the canvas being broken rather than the module being unavailable.
        if (!isEntryEnabled(index))
            return;

        startDragForEntry(index);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        // Only the click-activated kinds get here with a pending press; everything else already
        // started its drag on mouse-down.
        if (pressedIndex < 0)
            return;
        if (entries[(size_t)pressedIndex].kind != RowKind::Plugin)
            return; // the scan command is a button, not a drag source
        if (e.getDistanceFromDragStart() < kDragStartThresholdPx)
            return;

        const int index = pressedIndex;
        pressedIndex = -1;
        startDragForEntry(index);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        const int index = pressedIndex;
        pressedIndex = -1;
        if (index < 0 || e.mouseWasDraggedSinceMouseDown())
            return;
        activateRow(index);
    }

    // -------------------------------------------------------------------------
    // Test / inspection helpers
    // -------------------------------------------------------------------------

    /** Every draggable (non-header) entry, in display order — exactly the strings this component puts
     *  in the drag payload. Tests use this so a module added here is automatically covered instead of
     *  needing a parallel hand-kept list. */
    /** Names of the module TYPES the library offers — i.e. every row that maps to a factory entry.
     *  Filtered on RowKind::Module rather than "not a header": the sidebar also carries snippet rows
     *  and the "No snippets yet" placeholder, and neither is a module type callers can instantiate. */
    juce::StringArray getDraggableModuleNames() const {
        juce::StringArray names;
        for (const auto& entry : entries)
            if (entry.kind == RowKind::Module)
                names.add(entry.text);
        return names;
    }

    /** The section header a draggable entry sits under ("Sources", "Time FX", …), or an empty
     *  string when `moduleName` is not in the library. Tests use this to assert that other
     *  per-module groupings — cable colour categories, for one — stay in sync with the library's
     *  own sections instead of drifting behind a hand-kept copy of this list. */
    juce::String getSectionForModule(const juce::String& moduleName) const {
        juce::String currentHeader;
        for (const auto& entry : entries) {
            if (entry.isHeader)
                currentHeader = entry.text;
            else if (entry.text == moduleName)
                return currentHeader;
        }
        return {};
    }

    /** Returns the currently hovered entry index, or -1 when nothing is hovered. */
    int getHoveredIndex() const noexcept { return hoveredIndex; }

    /** Total number of entries (headers + items), collapsed or not. */
    int getEntryCount() const noexcept { return (int)entries.size(); }

    /** Display text of the entry at `index`, or an empty string when out of range. */
    juce::String getEntryText(int index) const {
        return (index >= 0 && index < (int)entries.size()) ? entries[index].text : juce::String();
    }

    const Entry& getEntry(int index) const { return entries[(size_t)index]; }

    /** Number of rows currently drawn — shrinks as sections collapse. */
    int getVisibleRowCount() const { return (int)buildRows().size(); }

    /** Index of the first draggable row, or -1. Lets callers locate a row without hard-coding a
     *  y-offset that shifts every time the sidebar gains a section. */
    int getFirstDraggableEntryIndex() const {
        for (const auto& row : buildRows())
            if (isDraggableEntry(row.entryIndex))
                return row.entryIndex;
        return -1;
    }

    /** Vertical centre of an entry's row, or -1 when the entry is not currently visible. */
    int getRowCentreY(int entryIndex) const {
        for (const auto& row : buildRows())
            if (row.entryIndex == entryIndex)
                return row.y + row.height / 2;
        return -1;
    }

    /** Returns the entry index whose visible row contains contentY, or -1 if none.
     *  Takes a *content-space* y — the same space buildRows() and getRowCentreY() report, which is
     *  component space only while the panel is scrolled to the top. Mouse handlers go through
     *  getEntryIndexAtComponentY() instead. */
    int getEntryIndexAt(int contentY) const {
        for (const auto& row : buildRows())
            if (contentY >= row.y && contentY < row.y + row.height)
                return row.entryIndex;
        return -1;
    }

    /** Component-space y → entry index, or -1. The search field and collapse strip are pinned
     *  chrome, so a row scrolled underneath them is never a hit. */
    int getEntryIndexAtComponentY(int y) const {
        if (isInPinnedChrome(y))
            return -1;
        return getEntryIndexAt(y + scrollOffset);
    }

private:
    float targetProgressFor(const juce::String& header) const { return isSectionCollapsed(header) ? 1.0f : 0.0f; }

    void snapSectionProgressToTargets() {
        for (const auto& entry : entries)
            if (entry.kind == RowKind::Header)
                sectionProgress[entry.text] = targetProgressFor(entry.text);
    }

    /** Tweens every section from where it is now to where the logical state says it should be.
     *  One driver covers all sections so "collapse all" folds them together rather than firing
     *  nine competing animations. */
    void startCollapseAnimation() {
        // No VBlank to drive frames when we're not on screen (headless tests, or a restore before
        // the window exists), so land on the final layout immediately.
        if (!isShowing()) {
            snapSectionProgressToTargets();
            clampHoverToVisibleRow();
            updateScrollBar();
            repaint();
            return;
        }

        if (!vblankUpdater.has_value())
            vblankUpdater.emplace(this);

        // Snapshot the *current* values, so retargeting mid-flight eases on from where it is
        // rather than snapping back to the start.
        std::map<juce::String, float> from;
        std::map<juce::String, float> to;
        for (const auto& entry : entries) {
            if (entry.kind != RowKind::Header)
                continue;
            from[entry.text] = getSectionProgress(entry.text);
            to[entry.text] = targetProgressFor(entry.text);
        }

        collapseAnim.start(
            *vblankUpdater, kCollapseAnimMs, synth::ui::easeInOutCubic,
            [this, from, to](float t) {
                for (const auto& [header, start] : from)
                    sectionProgress[header] = start + (to.at(header) - start) * t;
                updateScrollBar();
                repaint();
            },
            [this] {
                snapSectionProgressToTargets();
                clampHoverToVisibleRow();
                updateScrollBar();
                repaint();
            });
    }

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override {
        if (bar == &verticalScrollBar)
            setScrollOffset(juce::roundToInt(newRangeStart));
    }

    /** Slim themed width (AppLookAndFeel::kScrollbarWidth), or the JUCE default headlessly. */
    int getScrollBarWidth() const { return juce::jmax(4, getLookAndFeel().getDefaultScrollbarWidth()); }

    int getRowContentWidth() const {
        return getWidth() - (verticalScrollBar.isVisible() ? verticalScrollBar.getWidth() : 0);
    }

    /** Shows/hides and re-ranges the scrollbar for the current row set, and re-clamps the offset.
     *  Must run after anything that changes the content height — a resize, a collapse, a search
     *  filter, or a snippet refresh — or a shrinking list would leave the view scrolled past its
     *  own end. */
    void updateScrollBar() {
        const int viewportHeight = getHeight() - kPinnedChromeHeight;
        const int scrollableHeight = getTotalContentHeight() - kPinnedChromeHeight;
        const bool needed = viewportHeight > 0 && scrollableHeight > viewportHeight;

        verticalScrollBar.setVisible(needed);
        setScrollOffset(scrollOffset); // re-clamp against the new maximum (0 when it no longer fits)

        if (!needed)
            return;

        const int barWidth = getScrollBarWidth();
        verticalScrollBar.setBounds(getWidth() - barWidth, kPinnedChromeHeight, barWidth, viewportHeight);
        verticalScrollBar.setSingleStepSize((double)kItemHeight);
        verticalScrollBar.setRangeLimits(0.0, (double)scrollableHeight, juce::dontSendNotification);
        verticalScrollBar.setCurrentRange((double)scrollOffset, (double)viewportHeight, juce::dontSendNotification);
    }

    bool isDraggableEntry(int index) const {
        if (index < 0 || index >= (int)entries.size())
            return false;
        const auto kind = entries[(size_t)index].kind;
        return kind == RowKind::Module || kind == RowKind::Snippet || kind == RowKind::Plugin;
    }

    bool isActionEntry(int index) const {
        return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind == RowKind::Action;
    }

    /** Draggable rows plus the command rows — everything that highlights on hover. */
    bool isInteractiveEntry(int index) const { return isDraggableEntry(index) || isActionEntry(index); }

    bool isHeaderEntry(int index) const {
        return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind == RowKind::Header;
    }

    static synth::PluginIdentity identityForEntry(const Entry& entry) {
        synth::PluginIdentity identity;
        identity.format = entry.detail;
        identity.name = entry.text;
        identity.uid = entry.pluginUid;
        return identity;
    }

    juce::String tooltipForEntry(int index) const {
        const auto& entry = entries[(size_t)index];
        switch (entry.kind) {
        case RowKind::Snippet:
            return snippetDescription(entry.text, entry.moduleCount);
        case RowKind::Plugin:
            return pluginDescription(entry.text, entry.detail);
        case RowKind::Action:
            return scanRowDescription(!plugins.empty());
        default:
            break;
        }
        juce::String tip = descriptionFor(entry.text);
        if (!isEntryEnabled(index))
            tip += " (already in this patch)";
        return tip;
    }

    /** Starts the DragAndDrop session for a draggable row. Every payload rides the same channel and
     *  is told apart by its prefix — plain text is a module type, "snippet:" a saved group,
     *  "plugin:" a scanned plugin identity. */
    void startDragForEntry(int index) {
        const auto& entry = entries[(size_t)index];

        juce::String payload = entry.text;
        if (entry.kind == RowKind::Snippet)
            payload = synth::SnippetManager::payloadForName(entry.text);
        else if (entry.kind == RowKind::Plugin)
            payload = identityForEntry(entry).toDragPayload();

        juce::Image dragImage(juce::Image::ARGB, 150, 30, true);
        juce::Graphics dg(dragImage);
        dg.setColour(juce::Colours::white);
        dg.setFont(juce::Font(juce::FontOptions(16.0f)));
        dg.drawText(entry.text, dragImage.getBounds(), juce::Justification::centred, false);

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            container->startDragging(payload, this, dragImage);
    }

    /** Drops a hover that a collapse (or a snippet-list refresh) just hid, so no highlight is
     *  painted for a row that is no longer on screen. */
    void clampHoverToVisibleRow() {
        if (hoveredIndex < 0)
            return;
        for (const auto& row : buildRows())
            if (row.entryIndex == hoveredIndex)
                return;
        hoveredIndex = -1;
    }

    void applySearchQuery(const juce::String& text) {
        if (searchQuery == text)
            return;
        searchQuery = text;
        clampHoverToVisibleRow();
        // New filters should show the first match, not leave the view parked halfway down a list
        // that just shrank.
        scrollOffset = 0;
        updateScrollBar();
        repaint();
    }

    bool sectionVisibleInSearch(size_t headerIndex, size_t end) const {
        const auto q = normalisedSearchQuery(searchQuery);
        if (textMatchesQuery(entries[headerIndex].text, q))
            return true;
        for (size_t j = headerIndex + 1; j < end; ++j)
            if (textMatchesQuery(entries[j].text, q))
                return true;
        return false;
    }

    bool childVisibleInSearch(const Entry& entry) const {
        const auto q = normalisedSearchQuery(searchQuery);
        return textMatchesQuery(entry.text, q) || textMatchesQuery(entry.section, q);
    }

    void applySearchEditorColours() {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        juce::Colour bg = juce::Colours::black.withAlpha(0.35f);
        juce::Colour text = juce::Colours::white;
        juce::Colour muted = juce::Colours::grey;
        juce::Colour outline = juce::Colours::grey.darker();
        if (lf != nullptr) {
            const auto& c = lf->getTheme().colors;
            bg = c.surface;
            text = c.textPrimary;
            muted = c.textMuted;
            outline = c.border;
        }
        searchEditor.setColour(juce::TextEditor::backgroundColourId, bg);
        searchEditor.setColour(juce::TextEditor::textColourId, text);
        searchEditor.setColour(juce::TextEditor::outlineColourId, outline);
        searchEditor.setColour(juce::TextEditor::focusedOutlineColourId, outline);
        searchEditor.setTextToShowWhenEmpty("Search modules...", muted);
    }

    static void drawHighlightedText(juce::Graphics& g, const juce::String& text, const juce::String& query,
                                    juce::Rectangle<int> bounds, const juce::Font& font, juce::Colour normal,
                                    juce::Colour highlightFill, juce::Colour highlightText) {
        g.setFont(font);
        const auto spans = highlightSpansFor(text, query);
        if (spans.empty()) {
            g.setColour(normal);
            g.drawText(text, bounds, juce::Justification::centredLeft, true);
            return;
        }

        const float baseX = (float)bounds.getX();
        for (const auto& span : spans) {
            const float preW = font.getStringWidthFloat(text.substring(0, span.start));
            const float matchW = font.getStringWidthFloat(text.substring(span.start, span.start + span.length));
            g.setColour(highlightFill);
            g.fillRoundedRectangle(baseX + preW - 1.0f, (float)bounds.getY() + 4.0f, matchW + 2.0f,
                                   juce::jmax(8.0f, (float)bounds.getHeight() - 8.0f), 2.0f);
        }

        juce::AttributedString as;
        as.setJustification(juce::Justification::centredLeft);
        as.setWordWrap(juce::AttributedString::none);
        int pos = 0;
        for (const auto& span : spans) {
            if (span.start > pos)
                as.append(text.substring(pos, span.start), font, normal);
            as.append(text.substring(span.start, span.start + span.length), font, highlightText);
            pos = span.start + span.length;
        }
        if (pos < text.length())
            as.append(text.substring(pos), font, normal);
        as.draw(g, bounds.toFloat());
    }

    /** @param progress 0 = open (pointing down) .. 1 = folded (pointing right). Drawn as the open
     *  triangle rotated by -90° * progress: for a square area the endpoints are exactly the two
     *  shapes this used to switch between, so 0 and 1 look identical to the old two-state version
     *  while everything in between is a real rotation. */
    static void drawChevron(juce::Graphics& g, juce::Rectangle<float> area, float progress, juce::Colour colour) {
        juce::Path p;
        p.addTriangle(area.getX(), area.getY(), area.getRight(), area.getY(), area.getCentreX(), area.getBottom());
        p.applyTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi * progress,
                                                         area.getCentreX(), area.getCentreY()));
        g.setColour(colour);
        g.fillPath(p);
    }

    // Map a category header string to its Icon enum value.
    static synth::theme::Icon categoryIconForHeader(const juce::String& header) {
        if (header.equalsIgnoreCase(kSnippetsHeader) || header.equalsIgnoreCase(kPluginsHeader))
            return synth::theme::Icon::CatUtility;
        if (header.equalsIgnoreCase("Sources"))
            return synth::theme::Icon::CatSources;
        if (header.equalsIgnoreCase("Sequencing"))
            return synth::theme::Icon::CatSequencing;
        if (header.startsWithIgnoreCase("Envelopes"))
            return synth::theme::Icon::CatEnvelopes;
        if (header.equalsIgnoreCase("Filters"))
            return synth::theme::Icon::CatFilters;
        if (header.startsWithIgnoreCase("Modulation"))
            return synth::theme::Icon::CatModulationFX;
        if (header.equalsIgnoreCase("Time FX"))
            return synth::theme::Icon::CatTimeFX;
        if (header.equalsIgnoreCase("Dynamics"))
            return synth::theme::Icon::CatDynamics;
        // "Utility" and any unrecognised headers fall back to CatUtility.
        return synth::theme::Icon::CatUtility;
    }

    /** Rebuilds the flat entry list: the Snippets section first (it holds what the user just made
     *  and reaches for most), then the fixed module catalogue. */
    void rebuildEntries() {
        entries.clear();

        auto addHeader = [this](const juce::String& text) {
            entries.push_back({text, true, RowKind::Header, {}, 0, {}, 0});
        };

        addHeader(kSnippetsHeader);
        if (snippets.isEmpty()) {
            // Keep the section visible when empty so the feature is discoverable at all.
            entries.push_back({"No snippets yet", false, RowKind::EmptyHint, kSnippetsHeader, 0, {}, 0});
        } else {
            for (const auto& snippet : snippets)
                entries.push_back({snippet.name, false, RowKind::Snippet, kSnippetsHeader, snippet.moduleCount, {}, 0});
        }

        struct Category {
            const char* header;
            std::vector<const char*> modules;
        };
        // One module per line: this list is the library's visible order, and letting it pack into a
        // grid turns inserting a module into a whole-block reflow instead of a one-line diff.
        // clang-format off
        static const std::vector<Category> catalogue = {
            {"Sources", {
                "Oscillator",
                "Wavetable",
                "Noise",
                "Sampler",
                "LFO",
            }},
            {"Sequencing", {
                "Sequencer",
                "Poly Sequencer",
                "MidiKeyboard",
                "Poly MIDI",
                "External MIDI",
            }},
            {"Envelopes & Control", {
                "ADSR",
                "Envelope Follower",
                "VCA",
            }},
            {"Filters", {
                "Filter",
                "Parametric EQ",
            }},
            {"Modulation FX", {
                "Chorus",
                "Phaser",
                "Flanger",
                "Distortion",
                "Ring Modulator",
                "Bitcrusher",
                "Pitch Shifter",
            }},
            {"Time FX", {
                "Delay",
                "Reverb",
            }},
            {"Dynamics", {
                "Compressor",
                "Limiter",
            }},
            {"Utility", {
                "Macros",
                "Sample & Hold",
                "Comparator",
                "Voice Mixer",
                "Math",
            }},
            // Singletons — a patch holds at most one of each, so these rows grey out once the
            // canvas already has them (see isModuleAvailable).
            {"I/O", {
                "Audio Input",
                "Audio Output",
            }},
        };
        // clang-format on

        for (const auto& category : catalogue) {
            addHeader(category.header);
            for (const auto* moduleName : category.modules)
                entries.push_back({moduleName, false, RowKind::Module, category.header, 0, {}, 0});
        }

        // Plugins last: it is the only section whose contents come from outside this app, it is
        // empty until the user asks for a scan, and keeping it at the bottom means adding it did not
        // move a single existing row.
        addHeader(kPluginsHeader);
        entries.push_back({kScanPluginsRowText, false, RowKind::Action, kPluginsHeader, 0, {}, 0});

        // Sub-grouped by format (VST3, AudioUnit, …) so a big scan doesn't read as one undifferentiated
        // blob. Groups sort alphabetically by format name, rows inside a group by plugin name —
        // sorted here outright rather than trusting the caller's order (PluginScanService happens
        // to hand the list name-sorted, but setPlugins() makes no such promise). One sub-label per
        // format — even a single-format library still gets one, so the section always reads the
        // same way rather than special-casing the common case.
        std::vector<synth::PluginIdentity> sortedPlugins = plugins;
        std::sort(sortedPlugins.begin(), sortedPlugins.end(),
                  [](const synth::PluginIdentity& a, const synth::PluginIdentity& b) {
                      return a.format != b.format ? a.format < b.format : a.name < b.name;
                  });
        juce::String currentFormat;
        bool haveFormat = false;
        for (const auto& plugin : sortedPlugins) {
            if (!haveFormat || plugin.format != currentFormat) {
                entries.push_back({plugin.format, false, RowKind::SubHeader, kPluginsHeader, 0, {}, 0});
                currentFormat = plugin.format;
                haveFormat = true;
            }
            entries.push_back({plugin.name, false, RowKind::Plugin, kPluginsHeader, 0, plugin.format, plugin.uid});
        }
    }

    std::vector<Entry> entries;
    juce::Array<synth::SnippetInfo> snippets;
    std::vector<synth::PluginIdentity> plugins;
    std::set<juce::String> collapsedSections;
    int hoveredIndex = -1;        // -1 = no hover; updated on mouseMove/mouseExit only
    int pressedIndex = -1;        // row whose click is pending a mouseUp (Action / Plugin rows only)
    bool topStripHovered = false; // hover state for the collapse-all chrome

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
};
