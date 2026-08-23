#pragma once

#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

// MidiDestinationPicker — a searchable multi-select list for where a timeline track's bound
// Track In node sends MIDI. Lives inside a juce::CallOutBox (the caller builds and launches the
// callout; this class never does so itself, matching ColourPickerPopup's split between the
// component and its CallOutBox launch site).
//
// The graph is the truth: toggling a row applies the change through the caller's callback, then
// immediately re-pulls the provider and rebuilds every row from what it reports. A concurrent
// graph edit (or the apply itself failing for a reason the picker can't see, e.g. a stale
// binding) must never leave a checkbox showing something the graph doesn't actually have.
//
// Styling is entirely self-contained (paint() fills an opaque themed panel, every child colour is
// pushed explicitly) rather than assumed from the ambient LookAndFeel: a juce::CallOutBox launched
// with no parent component (see TimelineTrackHeaderComponent::openMidiDestinationsPicker) becomes a
// new top-level window that does not necessarily inherit synth::theme::AppLookAndFeel, which is
// what made this popup read as an unstyled, near-transparent bubble before this fix.
namespace synth::ui {

class MidiDestinationPicker : public juce::Component {
public:
    struct Option {
        // Which section a destination lists under. Instruments first, then Other (e.g. ADSR) —
        // see MainComponent::getMidiDestinationOptions for how the real picker's options are
        // enumerated. A header is only ever drawn when BOTH groups are actually represented (see
        // rebuildRows()), so a caller that never sets this (every pre-existing option literal in
        // this codebase and its tests) renders exactly the flat, headerless list it always did.
        enum class Group { Instruments, Other };

        juce::String displayName;
        juce::uint32 nodeUid = 0;
        bool connected = false;
        Group group = Group::Instruments;
    };

    // refreshProvider re-enumerates the live destination list (called once up front, and again
    // after every toggle); applyConnection(nodeUid, connect) is the caller's hook into the graph.
    MidiDestinationPicker(std::function<std::vector<Option>()> refreshProvider,
                          std::function<void(juce::uint32, bool)> applyConnection)
        : refreshProvider_(std::move(refreshProvider))
        , applyConnection_(std::move(applyConnection)) {
        setComponentID("midiDestinationPicker");

        searchEditor_.setComponentID("midiDestinationSearch");
        searchEditor_.setMultiLine(false);
        searchEditor_.setReturnKeyStartsNewLine(false);
        searchEditor_.setEscapeAndReturnKeysConsumed(false);
        searchEditor_.setSelectAllWhenFocused(true);
        searchEditor_.setJustification(juce::Justification::centredLeft);
        searchEditor_.setBorder(juce::BorderSize<int>(0));
        searchEditor_.setIndents(6, 0);
        searchEditor_.setFont(juce::Font(juce::FontOptions(kRowFontSize)));
        searchEditor_.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible(searchEditor_);
        applySearchEditorColours();

        addAndMakeVisible(viewport_);
        viewport_.setViewedComponent(&rowColumn_, false);
        viewport_.setScrollBarsShown(true, false);

        // refreshRows() itself sizes the popup (getWidth()==0 here, so it falls back to kWidth) —
        // no separate initial setSize() call needed.
        refreshRows();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(kOuterPadding);
        searchEditor_.setBounds(bounds.removeFromTop(kSearchHeight));
        bounds.removeFromTop(4);
        viewport_.setBounds(bounds);
        layoutRowColumn();
    }

    // Paints an OPAQUE themed panel — surface fill, 1px border — rather than relying on
    // juce::CallOutBox's own (semi-transparent, unthemed-when-parentless) bubble background.
    void paint(juce::Graphics& g) override {
        juce::Colour bg = juce::Colours::darkgrey.darker(0.4f);
        juce::Colour border = juce::Colours::grey.darker();
        float radius = 6.0f;

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            bg = c.surface;
            border = c.border;
            radius = lf->getTheme().metrics.cornerRadius;
        }

        auto bounds = getLocalBounds().toFloat();
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    }

    // Re-resolves every cached colour (search field + every row) against whatever LookAndFeel is
    // active now — a theme switch, or this popup finally being parented under one, must not leave
    // stale colours behind. Mirrors ModuleLibraryComponent's own lookAndFeelChanged() discipline.
    void lookAndFeelChanged() override {
        applySearchEditorColours();
        applyRowColours();
        repaint();
    }

    // getLookAndFeel() is only reliable once this component is actually parented (see
    // ModuleLibraryComponent::parentHierarchyChanged for the same reasoning) — a CallOutBox
    // attaches its content component after construction, so re-pull colours the moment that
    // happens too, not just from the constructor's one-shot styling calls.
    void parentHierarchyChanged() override {
        applySearchEditorColours();
        applyRowColours();
    }

    // ---- Test seams -------------------------------------------------------------------------
    void setSearchTextForTest(const juce::String& text) {
        // dontSendNotification + an explicit applyFilter() call, mirroring
        // ModuleLibraryComponent::setSearchText — deterministic regardless of whether
        // juce::TextEditor's own change notification happens to run synchronously.
        searchEditor_.setText(text, juce::dontSendNotification);
        applyFilter();
    }
    juce::String getSearchTextForTest() const { return searchEditor_.getText(); }

    /** Currently-visible (post-filter) row display names, in list order — section headers
     *  included when they are showing (i.e. when both groups are represented and at least one of
     *  a header's rows still matches the filter). When there are no destinations at all, this
     *  returns the single hint row's text. */
    std::vector<juce::String> getVisibleRowNamesForTest() const {
        std::vector<juce::String> names;
        for (auto* row : rows_)
            if (row->isVisible())
                names.push_back(row->getDisplayedText());
        return names;
    }

    /** Toggles the row at `visibleIndex` among the currently-filtered rows (matching what
     *  getVisibleRowNamesForTest() enumerates) — a no-op when there is no such row or it is a
     *  non-interactive header/hint row. */
    void toggleRowForTest(int visibleIndex) {
        int i = 0;
        for (auto* row : rows_) {
            if (!row->isVisible())
                continue;
            if (i == visibleIndex) {
                row->toggleForTest();
                return;
            }
            ++i;
        }
    }

private:
    static constexpr int kWidth = 260;
    static constexpr int kMaxHeight = 360;
    static constexpr int kOuterPadding = 6;
    static constexpr int kSearchHeight = 26;
    static constexpr int kRowHeight = 24;
    static constexpr int kHeaderRowHeight = 20;
    static constexpr float kRowFontSize = 13.0f; // the token ModuleLibraryComponent's rows use

    // One row per destination option (a checkbox + label), one per section header ("Instruments"
    // / "Other"), or the single non-interactive "bind a Track In node first" hint — all modeled as
    // a Row so getVisibleRowNamesForTest()'s "read every visible row's text" logic never needs a
    // per-kind special case.
    class Row : public juce::Component {
    public:
        enum class Kind { Toggle, Header, Hint };

        Row(Kind kind, juce::String text, std::function<void()> onToggle)
            : kind_(kind)
            , onToggle_(std::move(onToggle)) {
            if (kind_ == Kind::Toggle) {
                // Decorative only: the tick glyph is drawn by the LookAndFeel, but the click is
                // handled by this Row's own mouseUp() (see below) so the whole row — not just the
                // small tick box — is the click target. setClickingTogglesState(false) would be
                // redundant here since the button never receives a click at all.
                toggle_.setInterceptsMouseClicks(false, false);
                addAndMakeVisible(toggle_);
            }
            label_.setText(text, juce::dontSendNotification);
            label_.setJustificationType(juce::Justification::centredLeft);
            label_.setInterceptsMouseClicks(false, false);
            label_.setFont(juce::Font(juce::FontOptions(kind_ == Kind::Header ? kHeaderFontSize : kRowFontSize)));
            addAndMakeVisible(label_);
        }

        void resized() override {
            auto bounds = getLocalBounds();
            if (kind_ == Kind::Toggle) {
                toggle_.setBounds(bounds.removeFromLeft(kTickAreaWidth));
                label_.setBounds(bounds.withTrimmedRight(4));
            } else {
                label_.setBounds(bounds.reduced(kSectionIndent, 0));
            }
        }

        // The whole row (not just the tick glyph — see the constructor comment) is the click
        // target for a Toggle row; header/hint rows are inert.
        void mouseUp(const juce::MouseEvent& e) override {
            if (kind_ == Kind::Toggle && onToggle_ && getLocalBounds().contains(e.getPosition()))
                onToggle_();
        }

        void setConnected(bool connected) { toggle_.setToggleState(connected, juce::dontSendNotification); }
        void setDisplayName(const juce::String& name) { label_.setText(name, juce::dontSendNotification); }

        juce::String getDisplayedText() const { return label_.getText(); }
        Kind getKind() const noexcept { return kind_; }

        // Mirrors the "call onClick()/the toggle callback directly" test idiom this app already
        // uses for buttons (e.g. TimelineTrackHeaderComponent's mute/solo/arm tests) — a faithful,
        // synchronous simulation of a real click landing anywhere on the row.
        void toggleForTest() {
            if (kind_ == Kind::Toggle && onToggle_)
                onToggle_();
        }

        void applyThemeColours(juce::Colour text, juce::Colour muted, juce::Colour tick, juce::Colour tickOff) {
            if (kind_ == Kind::Toggle) {
                toggle_.setColour(juce::ToggleButton::tickColourId, tick);
                toggle_.setColour(juce::ToggleButton::tickDisabledColourId, tickOff);
                label_.setColour(juce::Label::textColourId, text);
            } else {
                // Header and hint rows both read as secondary text — a section title and an
                // explanatory hint are equally "not the main content" of the list.
                label_.setColour(juce::Label::textColourId, muted);
            }
        }

        int getPreferredHeight() const noexcept { return kind_ == Kind::Header ? kHeaderRowHeight : kRowHeight; }

    private:
        static constexpr int kTickAreaWidth = 24;
        static constexpr int kSectionIndent = 4;
        static constexpr float kHeaderFontSize = 11.0f;

        Kind kind_;
        juce::ToggleButton toggle_;
        juce::Label label_;
        std::function<void()> onToggle_;
    };

    static juce::String groupLabel(Option::Group group) {
        return group == Option::Group::Instruments ? "Instruments" : "Other";
    }

    // Pulls the live option list and reconciles it against the rows already on screen.
    //
    // Options are always stable-sorted by group first (Instruments before Other) — a no-op
    // ordering change when only one group is present, which keeps every pre-existing caller (none
    // of which ever set Option::group) rendering in the exact order it always did.
    //
    // When the shape is unchanged (the overwhelmingly common case: a toggle only flips one
    // option's `connected` flag, it doesn't change which destinations exist or their groups), this
    // updates the existing toggle Rows IN PLACE rather than destroying and recreating them —
    // deliberately, because onRowToggled() calls this from inside the very Row's own click handler.
    // Destroying that Row here would delete a juce::Component while it's still unwinding its own
    // click dispatch (the same hazard ColourPickerPopup's rebuildFavouriteButtons() works around,
    // there by deferring instead of avoiding). Only a genuine shape change (different node set,
    // order, or group placement, or the empty<->populated transition) tears everything down and
    // rebuilds — which happens only from the constructor's initial call, never re-entrantly from a
    // row's own callback.
    void refreshRows() {
        std::vector<Option> newOptions;
        if (refreshProvider_)
            newOptions = refreshProvider_();
        std::stable_sort(newOptions.begin(), newOptions.end(),
                         [](const Option& a, const Option& b) { return a.group < b.group; });

        const bool sameShape = newOptions.size() == options_.size() && [&] {
            for (size_t i = 0; i < newOptions.size(); ++i)
                if (newOptions[i].nodeUid != options_[i].nodeUid || newOptions[i].group != options_[i].group)
                    return false;
            return true;
        }();
        const bool wasHintRow = options_.empty();
        const bool isHintRow = newOptions.empty();

        options_ = std::move(newOptions);

        if (sameShape && wasHintRow == isHintRow && !rows_.empty()) {
            for (size_t i = 0; i < toggleRows_.size() && i < options_.size(); ++i) {
                toggleRows_[i]->setDisplayName(options_[i].displayName);
                toggleRows_[i]->setConnected(options_[i].connected);
            }
        } else {
            rebuildRows();
        }

        applyRowColours();
        applyFilter();
        setSize(getWidth() > 0 ? getWidth() : kWidth, preferredHeight());
    }

    void rebuildRows() {
        rowColumn_.removeAllChildren();
        rows_.clear();
        toggleRows_.clear();
        ownedRows_.clear();

        if (options_.empty()) {
            addRow(std::make_unique<Row>(Row::Kind::Hint, "Bind this track to a Track In node first", nullptr));
            return;
        }

        // A header only earns its keep when it actually disambiguates something — a list that is
        // entirely one group (every option defaulting to Group::Instruments, as every caller that
        // predates this feature does) stays the flat, headerless list it always was.
        const bool hasInstruments = std::any_of(options_.begin(), options_.end(),
                                                [](const Option& o) { return o.group == Option::Group::Instruments; });
        const bool hasOther = std::any_of(options_.begin(), options_.end(),
                                          [](const Option& o) { return o.group == Option::Group::Other; });
        const bool showHeaders = hasInstruments && hasOther;

        std::optional<Option::Group> currentGroup;
        for (const auto& option : options_) {
            if (showHeaders && (!currentGroup.has_value() || *currentGroup != option.group)) {
                currentGroup = option.group;
                addRow(std::make_unique<Row>(Row::Kind::Header, groupLabel(option.group), nullptr));
            }
            const juce::uint32 nodeUid = option.nodeUid;
            auto row = std::make_unique<Row>(Row::Kind::Toggle, option.displayName,
                                             [this, nodeUid] { onRowToggled(nodeUid); });
            row->setConnected(option.connected);
            toggleRows_.push_back(row.get());
            addRow(std::move(row));
        }
    }

    void addRow(std::unique_ptr<Row> row) {
        rowColumn_.addAndMakeVisible(*row);
        rows_.push_back(row.get());
        ownedRows_.push_back(std::move(row));
    }

    // Applies the checkbox click through the caller's hook, then re-pulls the provider — the
    // graph is the truth, so the checkbox must reflect whatever actually happened, not what the
    // click asked for.
    void onRowToggled(juce::uint32 nodeUid) {
        bool wantConnect = true;
        for (const auto& option : options_) {
            if (option.nodeUid == nodeUid) {
                wantConnect = !option.connected;
                break;
            }
        }
        if (applyConnection_)
            applyConnection_(nodeUid, wantConnect);
        refreshRows();
    }

    // Search matches Toggle rows by display name; a Header shows only while at least one row in
    // its section still matches (an empty section heading left dangling above nothing is worse
    // than no heading at all), and the Hint row is never filtered out — there's nothing for the
    // search box to search among, so hiding it on top of that would leave an empty popup with no
    // explanation.
    void applyFilter() {
        const juce::String query = searchEditor_.getText().trim();
        Row* pendingHeader = nullptr;
        bool groupHasMatch = false;

        for (auto* row : rows_) {
            if (row->getKind() == Row::Kind::Header) {
                if (pendingHeader != nullptr)
                    pendingHeader->setVisible(groupHasMatch);
                pendingHeader = row;
                groupHasMatch = false;
                continue;
            }
            const bool matches = row->getKind() != Row::Kind::Toggle || query.isEmpty() ||
                                 row->getDisplayedText().containsIgnoreCase(query);
            row->setVisible(matches);
            if (matches)
                groupHasMatch = true;
        }
        if (pendingHeader != nullptr)
            pendingHeader->setVisible(groupHasMatch);

        layoutRowColumn();
    }

    void layoutRowColumn() {
        // getMaximumVisibleWidth() is 0 until the viewport itself has a size (e.g. during the
        // constructor's initial refreshRows(), before resized() ever runs) — resized() calls this
        // again once real bounds exist, so the pre-layout pass just leaves the column at width 0
        // rather than something visually meaningful.
        const int width = juce::jmax(0, viewport_.getMaximumVisibleWidth());
        int y = 0;
        for (auto* row : rows_) {
            if (!row->isVisible())
                continue;
            const int h = row->getPreferredHeight();
            row->setBounds(0, y, width, h);
            y += h;
        }
        rowColumn_.setSize(width, juce::jmax(y, 1));
    }

    // Total content height (search box + every row, UNFILTERED) capped at kMaxHeight — computed
    // from the full row list rather than the currently-filtered subset, so the popup does not
    // resize itself while the user is typing into the search box; the viewport handles whatever
    // does not fit, and a filtered-down list just leaves blank space below rather than shrinking
    // the popup around it.
    int preferredHeight() const {
        int rowsHeight = 0;
        for (auto* row : rows_)
            rowsHeight += row->getPreferredHeight();
        const int content = kOuterPadding + kSearchHeight + 4 + juce::jmax(rowsHeight, kRowHeight) + kOuterPadding;
        return juce::jlimit(kSearchHeight + kRowHeight + 2 * kOuterPadding + 4, kMaxHeight, content);
    }

    // Search field styling mirrors ModuleLibraryComponent::applySearchEditorColours() — resolve
    // theme tokens with a plain-colour fallback for when synth::theme::AppLookAndFeel is not the
    // active LookAndFeel (headless tests, or the top-level-window case in the class comment).
    void applySearchEditorColours() {
        juce::Colour bg = juce::Colours::black.withAlpha(0.35f);
        juce::Colour text = juce::Colours::white;
        juce::Colour muted = juce::Colours::grey;
        juce::Colour outline = juce::Colours::grey.darker();

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            // One shade darker than the popup's own panel fill (c.surface, see paint()) so the
            // field reads as inset rather than flush with the surrounding panel.
            bg = c.bg0;
            text = c.textPrimary;
            muted = c.textMuted;
            outline = c.border;
        }

        searchEditor_.setColour(juce::TextEditor::backgroundColourId, bg);
        searchEditor_.setColour(juce::TextEditor::textColourId, text);
        searchEditor_.setColour(juce::TextEditor::outlineColourId, outline);
        searchEditor_.setColour(juce::TextEditor::focusedOutlineColourId, outline);
        searchEditor_.setTextToShowWhenEmpty("Filter destinations...", muted);
    }

    // Pushes resolved theme colours into every row (same fallback pattern as
    // applySearchEditorColours()) — called after every rebuild and from lookAndFeelChanged() /
    // parentHierarchyChanged() so a theme switch re-skins rows already on screen.
    void applyRowColours() {
        juce::Colour text = juce::Colours::white;
        juce::Colour muted = juce::Colours::grey;
        juce::Colour tick = juce::Colours::lightblue;
        juce::Colour tickOff = juce::Colours::grey;

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            text = c.textPrimary;
            muted = c.textMuted;
            tick = c.accent;
            tickOff = c.border;
        }

        for (auto* row : rows_)
            row->applyThemeColours(text, muted, tick, tickOff);
    }

    std::function<std::vector<Option>()> refreshProvider_;
    std::function<void(juce::uint32, bool)> applyConnection_;

    juce::TextEditor searchEditor_;
    juce::Viewport viewport_;
    juce::Component rowColumn_;
    std::vector<Row*> rows_;       // every row, in display order (headers + toggles + hint)
    std::vector<Row*> toggleRows_; // just the Toggle rows, index-aligned with options_
    std::vector<std::unique_ptr<Row>> ownedRows_;
    std::vector<Option> options_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDestinationPicker)
};

} // namespace synth::ui
