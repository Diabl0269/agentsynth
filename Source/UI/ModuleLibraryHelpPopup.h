#pragma once

#include "../ShortcutManager.h"
#include "Theme/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// ModuleLibraryHelpPopup — the compact "?" guide popover for the module library sidebar's header
// row (see ModuleLibraryComponent::showHelpPopover() / createHelpPopupForTest()). Lives inside a
// juce::CallOutBox the same way MidiDestinationPicker does (see that file's header comment): the
// caller builds and launches the CallOutBox, this class never does so itself — which is also what
// keeps it constructible in a headless test with no display (see docs/timeline_panel_core.md's
// "opening a real popup/menu window is a protected/overridable seam, not a style choice" note —
// the same reasoning applies to a CallOutBox).
//
// Three plain disclosure sections, no accordion animation — "Using modules", "Your first patch"
// and "Key shortcuts" are short enough that an instant show/hide is all a one-shot guide needs.
// ModuleLibraryComponent's own animated fold exists for a list a user reopens constantly; this
// popup is read once and dismissed.
//
// Content is data first: every section's lines come from a pure static helper
// (usingModulesLines() / firstPatchSteps() / shortcutLines()) so a test can assert on the text
// without ever constructing a juce::Component — the same "pure static helper" idiom
// ModuleLibraryComponent::descriptionFor already uses. Kept as its own file/class (rather than
// nested in ModuleLibraryComponent) because it is a self-contained popup content component, the
// same split MidiDestinationPicker/ColourPickerPopup already draw between "the owner" and "the
// thing it pops up".
namespace synth::ui {

class ModuleLibraryHelpPopup : public juce::Component {
public:
    enum Section { UsingModules = 0, FirstPatch = 1, KeyShortcuts = 2 };
    static constexpr int kSectionCount = 3;

    // `shortcuts` is read-only and optional: null (every headless test, and any owner that has not
    // called ModuleLibraryComponent::setShortcutManager yet) falls back to each curated shortcut's
    // shipped default via shortcutHintFor's own null-manager contract, so the popover always shows
    // something sensible even before it is wired up.
    explicit ModuleLibraryHelpPopup(const ShortcutManager* shortcuts = nullptr) {
        setComponentID("moduleLibraryHelpPopup");
        addAndMakeVisible(viewport_);
        viewport_.setViewedComponent(&column_, false);
        viewport_.setScrollBarsShown(true, false);

        addSection(sectionTitle(UsingModules), usingModulesLines());
        addSection(sectionTitle(FirstPatch), firstPatchSteps());
        addSection(sectionTitle(KeyShortcuts), shortcutLines(shortcuts));

        applyThemeColours(); // also lays out the column at a sane fallback width (see layoutColumn)
        setSize(kWidth, preferredHeight());
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(kOuterPadding);
        viewport_.setBounds(bounds);
        layoutColumn();
    }

    // Opaque themed panel — a CallOutBox launched with no parent (see
    // ModuleLibraryComponent::showHelpPopover) becomes a top-level window that does not
    // necessarily inherit synth::theme::AppLookAndFeel. Mirrors MidiDestinationPicker::paint().
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
        auto b = getLocalBounds().toFloat();
        g.setColour(bg);
        g.fillRoundedRectangle(b, radius);
        g.setColour(border);
        g.drawRoundedRectangle(b.reduced(0.5f), radius, 1.0f);
    }

    void lookAndFeelChanged() override { applyThemeColours(); }
    void parentHierarchyChanged() override { applyThemeColours(); }

    // -------------------------------------------------------------------------
    // Pure content helpers — headless, no GUI needed at all.
    // -------------------------------------------------------------------------

    static juce::String sectionTitle(Section section) {
        switch (section) {
        case UsingModules:
            return "Using modules";
        case FirstPatch:
            return "Your first patch";
        case KeyShortcuts:
            return "Key shortcuts";
        }
        return {};
    }

    static juce::StringArray usingModulesLines() {
        return {"Drag a module from the library onto the canvas to add it.",
                "Drag from an output jack to an input jack to connect two modules.",
                "Drag a module near another one for a smart connection suggestion - hold Ctrl "
                "while dragging to insert it into an existing cable instead.",
                "Double-click a connected jack to disconnect it (toggle in Settings).",
                "Right-click a module for bypass, mute, delete and more."};
    }

    // The minimal audible patch — verified against Source/PresetManager.cpp's Default preset
    // (which patches an envelope into the VCA's CV input the same way) and VCAModule.h's own
    // processBlock: with nothing patched into the VCA's CV input, that CV reads as silence rather
    // than an implicit "fully open" value, so the ADSR connection below is not optional shaping —
    // without it the VCA outputs nothing at all.
    static juce::StringArray firstPatchSteps() {
        return {"1. Drag a Poly MIDI module onto the canvas - this is your MIDI source.",
                "2. Drag a MIDI Keyboard too (or bind a timeline track's piano roll instead), and "
                "connect it into Poly MIDI so it has notes to play.",
                "3. Drag an Oscillator, wire Poly MIDI's Poly Out to its Pitch input, and switch "
                "Poly on.",
                "4. Drag a VCA, wire the Oscillator's output into it, and switch Poly on.",
                "5. Drag an ADSR, wire Poly MIDI's Poly Out to its Gate input and switch Poly on, "
                "then wire the ADSR's output to the VCA's CV input - without this the VCA stays "
                "silent.",
                "6. Drag an Audio Output and wire the VCA's output to it.",
                "7. Play the on-screen MIDI Keyboard (or notes from the piano roll) to hear it."};
    }

    struct ShortcutEntry {
        const char* actionId;
        juce::KeyPress fallback;
    };

    // The curated highest-value bindings for a first-time user — undo/redo, transport, snap,
    // quantise, locator jumps, and the three panel toggles (see docs/shortcuts.md). Fallbacks
    // mirror ShortcutManager::resetToDefaults() exactly; they are used only when `manager` is null
    // (shortcutHintFor's own contract), so a headless popover still shows real keys.
    static const std::vector<ShortcutEntry>& curatedShortcuts() {
        static const std::vector<ShortcutEntry> table = {
            {"undo", juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)},
            {"redo", juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)},
            {"togglePlayback", juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers, 0)},
            {"timelineSnapToggle", juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0)},
            {"pianoRollQuantise", juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0)},
            {"timelineJumpToLocator1", juce::KeyPress('1', juce::ModifierKeys::altModifier, 0)},
            {"timelineJumpToLocator2", juce::KeyPress('2', juce::ModifierKeys::altModifier, 0)},
            {"toggleTimelinePanel", juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0)},
            {"toggleLibrary", juce::KeyPress('b', juce::ModifierKeys::commandModifier, 0)},
#if JUCE_MAC
            {"toggleAiPanel", juce::KeyPress('a', juce::ModifierKeys::ctrlModifier, 0)},
#else
            {"toggleAiPanel",
             juce::KeyPress('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)},
#endif
        };
        return table;
    }

    /** One "<Action> — <Key>" line per curatedShortcuts() entry. Both halves are resolved LIVE:
     *  the key via shortcutHintFor (so a rebind is reflected the next time this is called, never
     *  stale) and the label via ShortcutManager::getActionDescription (so the two can never drift
     *  apart from the Settings tab's own wording either). `manager` may be null — see the class
     *  comment. */
    static juce::StringArray shortcutLines(const ShortcutManager* manager) {
        juce::StringArray lines;
        for (const auto& entry : curatedShortcuts()) {
            const auto key = shortcutHintFor(manager, entry.actionId, entry.fallback);
            const auto label = ShortcutManager::getActionDescription(entry.actionId);
            lines.add(key.isNotEmpty() ? (label + "  -  " + key) : label);
        }
        return lines;
    }

    // -------------------------------------------------------------------------
    // Test seams
    // -------------------------------------------------------------------------

    juce::String getSectionTitleForTest(int index) const { return sections_[(size_t)index].title; }
    bool isSectionExpandedForTest(int index) const { return sections_[(size_t)index].expanded; }
    juce::StringArray getSectionLinesForTest(int index) const { return sections_[(size_t)index].lines; }

    /** Simulates clicking the header of section `index` — the same synchronous "call the handler
     *  directly" idiom this app's other headless popup tests use (see
     *  MidiDestinationPicker::Row::toggleForTest). */
    void toggleSectionForTest(int index) { sections_[(size_t)index].header->toggleForTest(); }

private:
    // ---- Header row: chevron + title; clicking anywhere on it toggles the section's body. ------
    class HeaderRow : public juce::Component {
    public:
        HeaderRow(juce::String title, std::function<void()> onToggle)
            : title_(std::move(title))
            , onToggle_(std::move(onToggle)) {}

        void setExpandedForPaint(bool expanded) {
            if (expanded_ == expanded)
                return;
            expanded_ = expanded;
            repaint();
        }

        void mouseUp(const juce::MouseEvent& e) override {
            if (onToggle_ && getLocalBounds().contains(e.getPosition()))
                onToggle_();
        }

        /** Mirrors MidiDestinationPicker::Row::toggleForTest — a faithful, synchronous simulation
         *  of a real click landing anywhere on the row. */
        void toggleForTest() {
            if (onToggle_)
                onToggle_();
        }

        void paint(juce::Graphics& g) override {
            // Chevron drawn as a rotated triangle path — the same geometry
            // ModuleLibraryComponent::drawChevron uses, duplicated here in miniature rather than
            // shared: pulling that header in here would invert the ownership direction between
            // the sidebar and its popup.
            juce::Path p;
            const juce::Rectangle<float> chevronArea(4.0f, (float)getHeight() * 0.5f - 4.0f, 8.0f, 8.0f);
            p.addTriangle(chevronArea.getX(), chevronArea.getY(), chevronArea.getRight(), chevronArea.getY(),
                          chevronArea.getCentreX(), chevronArea.getBottom());
            p.applyTransform(juce::AffineTransform::rotation(expanded_ ? 0.0f : -juce::MathConstants<float>::halfPi,
                                                             chevronArea.getCentreX(), chevronArea.getCentreY()));
            g.setColour(textColour_);
            g.fillPath(p);

            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText(title_, 20, 0, getWidth() - 24, getHeight(), juce::Justification::centredLeft);
        }

        void applyThemeColours(juce::Colour text) {
            textColour_ = text;
            repaint();
        }

    private:
        juce::String title_;
        bool expanded_ = true;
        juce::Colour textColour_ = juce::Colours::white;
        std::function<void()> onToggle_;
    };

    // ---- Body row: one bullet/step line, word-wrapped via juce::TextLayout. --------------------
    class TextRow : public juce::Component {
    public:
        explicit TextRow(juce::String text)
            : text_(std::move(text)) {}

        void applyThemeColours(juce::Colour text) {
            textColour_ = text;
            rebuildLayout();
        }

        /** Re-wraps for `width` and resizes to fit — called from layoutColumn() whenever the
         *  popup's content width is (re)established. A no-op at width <= 0 (the pre-layout pass
         *  before the viewport has real bounds — see MidiDestinationPicker::layoutRowColumn's
         *  identical comment), which leaves whatever size this row already had. */
        void layoutForWidth(int width) {
            if (width <= 0)
                return;
            width_ = width;
            rebuildLayout();
        }

        void paint(juce::Graphics& g) override { layout_.draw(g, getLocalBounds().toFloat()); }

        int getPreferredHeight() const noexcept { return juce::roundToInt(layout_.getHeight()) + kLineGap; }

    private:
        void rebuildLayout() {
            if (width_ <= 0)
                return;
            juce::AttributedString as;
            as.append(text_, juce::Font(juce::FontOptions(12.5f)), textColour_);
            as.setLineSpacing(3.0f);
            layout_.createLayout(as, (float)width_);
            setSize(width_, juce::roundToInt(layout_.getHeight()) + kLineGap);
        }

        static constexpr int kLineGap = 6;
        juce::String text_;
        juce::Colour textColour_ = juce::Colours::white;
        int width_ = 0;
        juce::TextLayout layout_;
    };

    struct SectionRow {
        juce::String title;
        juce::StringArray lines;
        bool expanded = true; // open by default — a first-time user should not need to click first
        HeaderRow* header = nullptr;
        std::vector<TextRow*> body;
    };

    void addSection(const juce::String& title, const juce::StringArray& lines) {
        const int index = (int)sections_.size();
        sections_.push_back({title, lines, true, nullptr, {}});
        auto& section = sections_.back();

        auto header = std::make_unique<HeaderRow>(title, [this, index] { toggleSection(index); });
        section.header = header.get();
        column_.addAndMakeVisible(*header);
        ownedRows_.push_back(std::move(header));

        for (const auto& line : lines) {
            auto row = std::make_unique<TextRow>(line);
            section.body.push_back(row.get());
            column_.addAndMakeVisible(*row);
            ownedRows_.push_back(std::move(row));
        }
    }

    void toggleSection(int index) {
        auto& section = sections_[(size_t)index];
        section.expanded = !section.expanded;
        section.header->setExpandedForPaint(section.expanded);
        layoutColumn();
        // Re-fits the outer popup to the now shorter/taller content — juce::CallOutBox tracks its
        // content component's size, so this is what makes collapsing a section actually shrink the
        // popup rather than leaving dead space (mirrors MidiDestinationPicker::refreshRows()).
        setSize(getWidth(), preferredHeight());
    }

    void applyThemeColours() {
        juce::Colour text = juce::Colours::white;
        juce::Colour muted = juce::Colours::lightgrey;
        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            text = c.textPrimary;
            muted = c.textMuted;
        }
        for (auto& section : sections_) {
            section.header->applyThemeColours(text);
            for (auto* row : section.body)
                row->applyThemeColours(muted);
        }
        layoutColumn();
    }

    // getMaximumVisibleWidth() is 0 until the viewport itself has real bounds (constructor time,
    // before resized() has ever run) — falls back to kContentWidth so the initial preferredHeight()
    // computation (and the first on-screen paint, if one somehow happened before a resize) is never
    // built against a zero-width, zero-height wrap. resized() re-runs this with the real width once
    // it exists.
    void layoutColumn() {
        const int viewportWidth = viewport_.getMaximumVisibleWidth();
        const int width = viewportWidth > 0 ? viewportWidth : kContentWidth;
        int y = 0;
        for (auto& section : sections_) {
            section.header->setBounds(0, y, width, kHeaderHeight);
            y += kHeaderHeight;
            for (auto* row : section.body) {
                row->layoutForWidth(width);
                row->setVisible(section.expanded);
                if (section.expanded) {
                    row->setBounds(0, y, width, row->getPreferredHeight());
                    y += row->getPreferredHeight();
                }
            }
            y += kSectionGap;
        }
        column_.setSize(width, juce::jmax(y, 1));
    }

    int preferredHeight() const {
        int rowsHeight = 0;
        for (auto& section : sections_) {
            rowsHeight += kHeaderHeight;
            if (section.expanded)
                for (auto* row : section.body)
                    rowsHeight += row->getPreferredHeight();
            rowsHeight += kSectionGap;
        }
        const int content = kOuterPadding * 2 + rowsHeight;
        return juce::jlimit(kHeaderHeight + kOuterPadding * 2, kMaxHeight, content);
    }

    static constexpr int kWidth = 340;
    static constexpr int kContentWidth = kWidth - 2 * 10 /* kOuterPadding */ - 14 /* scrollbar allowance */;
    static constexpr int kMaxHeight = 460;
    static constexpr int kOuterPadding = 10;
    static constexpr int kHeaderHeight = 24;
    static constexpr int kSectionGap = 8;

    juce::Viewport viewport_;
    juce::Component column_;
    std::vector<SectionRow> sections_;
    std::vector<std::unique_ptr<juce::Component>> ownedRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleLibraryHelpPopup)
};

} // namespace synth::ui
