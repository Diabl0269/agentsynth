#pragma once

#include "../SnippetManager.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <vector>

class ModuleLibraryComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::SettableTooltipClient {
public:
    /** What a row in the sidebar is, which decides how it paints and what a click does. */
    enum class RowKind {
        Header,   // category title — clicking it collapses/expands the section
        Module,   // draggable module name
        Snippet,  // draggable saved group (issue #156)
        EmptyHint // non-interactive placeholder, e.g. "No snippets yet"
    };

    struct Entry {
        juce::String text;
        bool isHeader = false;
        RowKind kind = RowKind::Module;
        juce::String section; // text of the header this row lives under ("" for headers)
        int moduleCount = 0;  // Snippet rows only — shown as a count suffix
    };

    // ---- Row geometry (pixels) ----
    static constexpr int kTopStripHeight = 24; // "Collapse all / Expand all" chrome above row 0
    static constexpr int kFirstRowY = 10;      // gap between the strip and the first entry row
    static constexpr int kHeaderHeight = 25;
    static constexpr int kHeaderGap = 5; // extra breathing room above every header but the first
    static constexpr int kItemHeight = 32;

    static constexpr const char* kSnippetsHeader = "Snippets";

    ModuleLibraryComponent() {
        // The flat entry list is rebuilt rather than assigned literally, because it now has to be
        // regenerated whenever the snippet list changes. The module catalogue itself lives in
        // rebuildEntries() — add new modules there.
        rebuildEntries();
        setMouseCursor(juce::MouseCursor::NormalCursor);
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
        repaint();
    }

    int getSnippetCount() const noexcept { return snippets.size(); }

    /** Invoked when the user picks "Delete Snippet" from a snippet row's context menu. */
    std::function<void(const juce::String&)> onSnippetDeleteRequested;

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
        clampHoverToVisibleRow();
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
        clampHoverToVisibleRow();
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
        clampHoverToVisibleRow();
        repaint();
    }

    /** Fired whenever the collapse state changes through user interaction, so the owner can
     *  persist it. */
    std::function<void()> onCollapseStateChanged;

    // -------------------------------------------------------------------------
    // Pure static helpers — callable headlessly (no GUI / MessageManager needed)
    // -------------------------------------------------------------------------

    /** Returns a one-line description for a known module name, or a generic
     *  fallback string for unknown names. */
    static juce::String descriptionFor(const juce::String& moduleName) {
        if (moduleName.equalsIgnoreCase("Oscillator"))
            return "Generates audio waveforms (sine, saw, square, triangle).";
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
            return "Converts MIDI input into polyphonic pitch and gate signals.";
        if (moduleName.equalsIgnoreCase("External MIDI"))
            return "Routes external MIDI device input into the patch graph.";
        if (moduleName.equalsIgnoreCase("ADSR"))
            return "Attack-Decay-Sustain-Release envelope generator.";
        if (moduleName.equalsIgnoreCase("VCA"))
            return "Voltage-controlled amplifier — controls signal amplitude via CV.";
        if (moduleName.equalsIgnoreCase("Filter"))
            return "Multi-mode resonant filter (low-pass, high-pass, band-pass).";
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
        // Generic fallback for any unrecognised module name.
        return "Audio processing module.";
    }

    /** Tooltip for a saved snippet row. */
    static juce::String snippetDescription(const juce::String& name, int moduleCount) {
        return "Snippet \"" + name + "\" — " + juce::String(moduleCount) +
               (moduleCount == 1 ? " module. " : " modules. ") + "Drag onto the canvas to insert the whole group.";
    }

    // -------------------------------------------------------------------------
    // Layout
    // -------------------------------------------------------------------------

    struct Row {
        int entryIndex;
        int y;
        int height;
    };

    /** Visible rows, top to bottom. Rows inside a collapsed section are omitted entirely.
     *  Painting and hit-testing share this one layout pass, so they cannot disagree about where a
     *  row is — they used to duplicate the y-advance arithmetic. */
    std::vector<Row> buildRows() const {
        std::vector<Row> rows;
        int y = kTopStripHeight + kFirstRowY;
        bool seenHeader = false;

        for (int i = 0; i < (int)entries.size(); ++i) {
            const auto& entry = entries[(size_t)i];

            if (entry.kind == RowKind::Header) {
                if (seenHeader)
                    y += kHeaderGap;
                seenHeader = true;
                rows.push_back({i, y, kHeaderHeight});
                y += kHeaderHeight;
                continue;
            }

            if (isSectionCollapsed(entry.section))
                continue;

            rows.push_back({i, y, kItemHeight});
            y += kItemHeight;
        }
        return rows;
    }

    /** Total pixel height of the currently visible content. */
    int getTotalContentHeight() const {
        auto rows = buildRows();
        return rows.empty() ? kTopStripHeight + kFirstRowY : rows.back().y + rows.back().height + kFirstRowY;
    }

    /** True when y falls inside the collapse-all chrome above the first row. */
    static bool isInTopStrip(int y) noexcept { return y >= 0 && y < kTopStripHeight; }

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

        // ---- Top strip: one control to fold the whole library away ----
        {
            const bool allCollapsed = areAllSectionsCollapsed();
            g.setColour(topStripHovered ? accentColour : mutedColour);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(allCollapsed ? "EXPAND ALL" : "COLLAPSE ALL", 10, 2, getWidth() - 20, kTopStripHeight - 4,
                       juce::Justification::centredRight);
        }

        for (const auto& row : buildRows()) {
            const auto& entry = entries[(size_t)row.entryIndex];

            if (entry.kind == RowKind::Header) {
                const bool collapsed = isSectionCollapsed(entry.text);

                // Disclosure chevron drawn as a path — glyph coverage for ▾/▸ is not guaranteed
                // across the embedded typefaces (see the theming font limitation).
                drawChevron(g, juce::Rectangle<float>(8.0f, (float)row.y + 6.0f, 8.0f, 8.0f), collapsed, headerColour);

                // Category icon at x=20 (null-guarded — no-op when LnF absent).
                synth::theme::Icon catIcon = categoryIconForHeader(entry.text);
                const juce::Drawable* icon = (lf != nullptr) ? lf->peekIcon(catIcon) : nullptr;

                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                if (icon != nullptr) {
                    icon->drawWithin(g, juce::Rectangle<float>(20.0f, (float)row.y + 2.0f, 16.0f, 16.0f),
                                     juce::RectanglePlacement::centred, 1.0f);
                    g.setColour(headerColour);
                    g.drawText(entry.text.toUpperCase(), 40, row.y, getWidth() - 50, 20,
                               juce::Justification::centredLeft);
                } else {
                    g.setColour(headerColour);
                    g.drawText(entry.text.toUpperCase(), 20, row.y, getWidth() - 30, 20,
                               juce::Justification::centredLeft);
                }
                continue;
            }

            if (entry.kind == RowKind::EmptyHint) {
                g.setColour(mutedColour.withAlpha(0.7f));
                g.setFont(juce::Font(juce::FontOptions(13.0f)));
                g.drawText(entry.text, 20, row.y, getWidth() - 40, kItemHeight - 4, juce::Justification::centredLeft);
                continue;
            }

            // Draggable row (module or snippet).
            if (row.entryIndex == hoveredIndex) {
                g.setColour(accentColour.withAlpha(0.12f));
                g.fillRect(0, row.y, getWidth(), kItemHeight);
            }

            g.setColour(itemColour);
            g.setFont(juce::Font(juce::FontOptions(16.0f)));
            g.drawText(entry.text, 20, row.y, getWidth() - 60, kItemHeight - 4, juce::Justification::centredLeft);

            if (entry.kind == RowKind::Snippet) {
                g.setColour(mutedColour);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                g.drawText("(" + juce::String(entry.moduleCount) + ")", getWidth() - 44, row.y, 34, kItemHeight - 4,
                           juce::Justification::centredRight);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Mouse events
    // -------------------------------------------------------------------------

    void mouseMove(const juce::MouseEvent& e) override {
        const bool wasTopStripHovered = topStripHovered;
        topStripHovered = isInTopStrip(e.y);

        const int entryUnderMouse = getEntryIndexAt(e.y);
        // Only draggable rows can be hovered; headers and hints clamp to -1.
        const int newIndex = isDraggableEntry(entryUnderMouse) ? entryUnderMouse : -1;

        if (newIndex != hoveredIndex || topStripHovered != wasTopStripHovered) {
            hoveredIndex = newIndex;

            // Update tooltip: the shared TooltipWindow (owned by MainComponent) reads
            // this component's tooltip string on each hover. Setting it here on hover
            // change means each draggable row surfaces its per-module description.
            if (topStripHovered) {
                setTooltip("Collapse or expand every category in the library.");
            } else if (hoveredIndex >= 0) {
                const auto& entry = entries[(size_t)hoveredIndex];
                setTooltip(entry.kind == RowKind::Snippet ? snippetDescription(entry.text, entry.moduleCount)
                                                          : descriptionFor(entry.text));
            } else {
                setTooltip({});
            }

            repaint();
        }

        // Update cursor: grab hand for draggable items, pointing hand for the clickable chrome.
        if (hoveredIndex >= 0)
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        else if (topStripHovered || isHeaderEntry(entryUnderMouse))
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
        if (isInTopStrip(e.y)) {
            toggleAllSections();
            return;
        }

        const int index = getEntryIndexAt(e.y);
        if (index < 0 || index >= (int)entries.size())
            return;

        const auto& entry = entries[(size_t)index];

        if (entry.kind == RowKind::Header) {
            toggleSection(entry.text);
            return;
        }

        if (entry.kind == RowKind::EmptyHint)
            return;

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

        // Snippet payloads carry a prefix so the canvas can tell a group drop from a module drop
        // on the same DragAndDrop channel.
        const juce::String payload =
            (entry.kind == RowKind::Snippet) ? synth::SnippetManager::payloadForName(entry.text) : entry.text;

        juce::Image dragImage(juce::Image::ARGB, 150, 30, true);
        juce::Graphics dg(dragImage);
        dg.setColour(juce::Colours::white);
        dg.setFont(juce::Font(juce::FontOptions(16.0f)));
        dg.drawText(entry.text, dragImage.getBounds(), juce::Justification::centred, false);

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
            container->startDragging(payload, this, dragImage);
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

    /** Returns the entry index whose visible row contains mouseY, or -1 if none. */
    int getEntryIndexAt(int mouseY) const {
        for (const auto& row : buildRows())
            if (mouseY >= row.y && mouseY < row.y + row.height)
                return row.entryIndex;
        return -1;
    }

private:
    bool isDraggableEntry(int index) const {
        if (index < 0 || index >= (int)entries.size())
            return false;
        const auto kind = entries[(size_t)index].kind;
        return kind == RowKind::Module || kind == RowKind::Snippet;
    }

    bool isHeaderEntry(int index) const {
        return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind == RowKind::Header;
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

    static void drawChevron(juce::Graphics& g, juce::Rectangle<float> area, bool collapsed, juce::Colour colour) {
        juce::Path p;
        if (collapsed) {
            // Pointing right — section folded.
            p.addTriangle(area.getX(), area.getY(), area.getX(), area.getBottom(), area.getRight(), area.getCentreY());
        } else {
            // Pointing down — section open.
            p.addTriangle(area.getX(), area.getY(), area.getRight(), area.getY(), area.getCentreX(), area.getBottom());
        }
        g.setColour(colour);
        g.fillPath(p);
    }

    // Map a category header string to its Icon enum value.
    static synth::theme::Icon categoryIconForHeader(const juce::String& header) {
        if (header.equalsIgnoreCase(kSnippetsHeader))
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

        auto addHeader = [this](const juce::String& text) { entries.push_back({text, true, RowKind::Header, {}, 0}); };

        addHeader(kSnippetsHeader);
        if (snippets.isEmpty()) {
            // Keep the section visible when empty so the feature is discoverable at all.
            entries.push_back({"No snippets yet", false, RowKind::EmptyHint, kSnippetsHeader, 0});
        } else {
            for (const auto& snippet : snippets)
                entries.push_back({snippet.name, false, RowKind::Snippet, kSnippetsHeader, snippet.moduleCount});
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
                "Voice Mixer",
                "Math",
            }},
        };
        // clang-format on

        for (const auto& category : catalogue) {
            addHeader(category.header);
            for (const auto* moduleName : category.modules)
                entries.push_back({moduleName, false, RowKind::Module, category.header, 0});
        }
    }

    std::vector<Entry> entries;
    juce::Array<synth::SnippetInfo> snippets;
    std::set<juce::String> collapsedSections;
    int hoveredIndex = -1;        // -1 = no hover; updated on mouseMove/mouseExit only
    bool topStripHovered = false; // hover state for the collapse-all chrome
};
