// WavetableTabStrip.cpp -- the Wavetable card's tab buttons, the parameter-name -> page table, page
// visibility and the pinned-row / strip / active-page layout. The controls themselves stay owned by
// ModuleComponent (which builds them generically in createControls()); this class only borrows
// them, so cable-drop targeting, modulation rings and MIDI Learn keep reading ModuleComponent's own
// slider arrays unchanged.
#include "ModuleComponent.h"
#include "ModuleComponentInternal.h"

#include <algorithm>

using namespace detail;

namespace {
// Page titles, and the control names each page owns. Names are the parameter display names
// (`param->getName(100)`), which is what the slider/combo labels carry.
struct Page {
    const char* title;
    const char* members; // space-free, '|'-separated display names
};

const Page kPages[] = {
    {"Tune", "Octave|Coarse|Fine|Level"},
    {"Unison", "Unison|Detune|Stack|Blend|Width"},
    {"Phase", "Phase|Rand Phase|Spread"},
    {"Sub", "Sub|Sub Oct|Sub Wave|Pan|Sync In"},
    {"File", "Import|Interp"},
};
static_assert((int)(sizeof(kPages) / sizeof(kPages[0])) == WavetableTabStrip::kNumPages);

constexpr int kKnobColumnsWide = kKnobColumns * 2; // double-width card
// Three across rather than two: no page has more than three combos, so this keeps every page's
// selectors on one row and takes the tallest page (Sub) from 172px to 124px.
constexpr int kComboColumns = 3;
constexpr int kComboRowHeight = kLabelHeight + kRowHeight + 6;
} // namespace

WavetableTabStrip::WavetableTabStrip(int radioGroupId) {
    for (int page = 0; page < kNumPages; ++page) {
        auto* tab = tabs.add(new juce::TextButton(kPages[page].title));
        tab->setComponentID("wtTab" + juce::String(page));
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(radioGroupId);
        tab->setToggleState(page == activePage, juce::dontSendNotification);
        tab->setConnectedEdges((page > 0 ? juce::Button::ConnectedOnLeft : 0) |
                               (page < kNumPages - 1 ? juce::Button::ConnectedOnRight : 0));
        tab->onClick = [this, page] { selectPage(page); };
        addAndMakeVisible(tab);
    }
}

int WavetableTabStrip::pageFor(const juce::String& controlName) {
    // Position and Warp are what you actually perform with, so they stay above the strip;
    // Table belongs with the display it selects.
    if (controlName == "Position" || controlName == "Warp" || controlName == "Warp Amt")
        return kPinned;
    if (controlName == "Table")
        return kChrome;

    for (int page = 0; page < kNumPages; ++page)
        for (const auto& member : juce::StringArray::fromTokens(kPages[page].members, "|", ""))
            if (member == controlName)
                return page;

    return 0; // anything unclassified lands on the first page rather than vanishing
}

void WavetableTabStrip::addSlider(juce::Slider& slider, juce::Label& label) {
    sliders.push_back({&slider, &label, pageFor(label.getText())});
}

void WavetableTabStrip::addCombo(juce::ComboBox& combo, juce::Label& label) {
    combos.push_back({&combo, &label, pageFor(label.getText())});
}

bool WavetableTabStrip::isChromeCombo(const juce::ComboBox& combo) const {
    return std::any_of(combos.begin(), combos.end(),
                       [&combo](const ComboEntry& e) { return e.control == &combo && e.page == kChrome; });
}

/** A tab click lands here; re-selecting the active page is a no-op.
 *
 *  onPageChanged is where the owner re-lays the card and tells the graph the card's content
 *  changed: a page switch can change which knob is visible under the owner's modulation-target
 *  lookups, so an AttenuverterChain cable re-anchored onto a now-hidden (or newly-visible) knob
 *  must recompute. That goes through the owner's one canvas-repaint seam, which is what
 *  invalidates the memoized cable list (docs/layout/cables.md). */
void WavetableTabStrip::selectPage(int page) {
    if (activePage == page)
        return;
    activePage = page;
    applyVisibility();
    if (onPageChanged)
        onPageChanged();
}

void WavetableTabStrip::applyVisibility() {
    const auto onActivePage = [this](int page) { return page == kPinned || page == activePage; };

    for (const auto& e : sliders) {
        const bool show = onActivePage(e.page);
        e.control->setVisible(show);
        e.label->setVisible(show);
    }
    for (const auto& e : combos) {
        const bool show = e.page == kChrome || onActivePage(e.page);
        e.control->setVisible(show);
        e.label->setVisible(show);
    }

    for (int page = 0; page < tabs.size(); ++page)
        tabs[page]->setToggleState(page == activePage, juce::dontSendNotification);
}

void WavetableTabStrip::resized() {
    const int tabW = getWidth() / std::max(1, tabs.size());
    for (int page = 0; page < tabs.size(); ++page)
        tabs[page]->setBounds(page * tabW, 0, tabW, getHeight());
}

int WavetableTabStrip::getTallestPageHeight() const {
    int tallest = 0;
    for (int page = 0; page < kNumPages; ++page) {
        const int pageCombos =
            (int)std::count_if(combos.begin(), combos.end(), [page](const ComboEntry& e) { return e.page == page; });
        const int pageKnobs =
            (int)std::count_if(sliders.begin(), sliders.end(), [page](const SliderEntry& e) { return e.page == page; });

        const int comboRows = (pageCombos + kComboColumns - 1) / kComboColumns;
        const int knobRows = (pageKnobs + kKnobColumnsWide - 1) / kKnobColumnsWide;
        tallest = std::max(tallest, comboRows * kComboRowHeight + knobRows * (kLabelHeight + kKnobHeight));
    }
    return tallest;
}

void WavetableTabStrip::layoutPinnedRow(int y, int contentX, int contentW) {
    const int cellW = contentW / 3;

    int col = 0;
    for (const auto& e : sliders) {
        if (e.page != kPinned)
            continue;
        const int x = contentX + (col == 0 ? 0 : cellW * 2);
        e.label->setBounds(x, y, cellW, kLabelHeight);
        e.control->setBounds(x, y + kLabelHeight, cellW, kKnobHeight);
        ++col;
    }
    for (const auto& e : combos) {
        if (e.page != kPinned)
            continue;
        // Label on the knobs' label line, combo vertically centred against the knobs, so the
        // three pinned controls read as one row rather than a stagger.
        const int x = contentX + cellW;
        e.label->setBounds(x + 6, y, cellW - 12, kLabelHeight);
        e.control->setBounds(x + 6, y + kLabelHeight + (kKnobHeight - kRowHeight) / 2, cellW - 12, kRowHeight);
    }
}

void WavetableTabStrip::layoutActivePage(int y, int contentX, int contentW) {
    const int knobWidth = contentW / kKnobColumnsWide;
    const int comboCellW = contentW / kComboColumns;

    int comboSlot = 0;
    for (const auto& e : combos) {
        if (e.page != activePage)
            continue;
        const int row = comboSlot / kComboColumns;
        const int x = contentX + (comboSlot % kComboColumns) * comboCellW;
        const int rowY = y + row * kComboRowHeight;
        e.label->setBounds(x, rowY, comboCellW - 8, kLabelHeight);
        e.control->setBounds(x, rowY + kLabelHeight, comboCellW - 8, kRowHeight);
        ++comboSlot;
    }
    y += ((comboSlot + kComboColumns - 1) / kComboColumns) * kComboRowHeight;

    const int pageKnobCount = (int)std::count_if(sliders.begin(), sliders.end(),
                                                 [this](const SliderEntry& e) { return e.page == activePage; });

    int knobSlot = 0;
    for (const auto& e : sliders) {
        if (e.page != activePage)
            continue;
        const int row = knobSlot / kKnobColumnsWide;
        const int col = knobSlot % kKnobColumnsWide;

        // Centre each row. Most pages carry fewer than kKnobColumnsWide knobs, and left-aligning
        // them stranded half the card's width as dead space.
        const int inThisRow = std::min(kKnobColumnsWide, pageKnobCount - row * kKnobColumnsWide);
        const int rowIndent = (contentW - inThisRow * knobWidth) / 2;

        const int x = contentX + rowIndent + col * knobWidth;
        const int rowY = y + row * (kLabelHeight + kKnobHeight);
        e.label->setBounds(x, rowY, knobWidth, kLabelHeight);
        e.control->setBounds(x, rowY + kLabelHeight, knobWidth, kKnobHeight);
        ++knobSlot;
    }
}

int WavetableTabStrip::layoutBody(int y, int contentX, int contentW, bool apply) {
    // --- Pinned row: the two performance controls, with Warp's mode selector between them ---
    if (apply)
        layoutPinnedRow(y, contentX, contentW);
    y += kLabelHeight + kKnobHeight + 10;

    // --- Tab strip (this component's own bounds; resized() places the buttons) ---
    if (apply)
        setBounds(contentX, y, contentW, kRowHeight);
    y += kRowHeight + 8;

    // --- Active page; the card is sized against the tallest page so a tab switch never resizes it ---
    if (apply)
        layoutActivePage(y, contentX, contentW);

    return y + getTallestPageHeight() + 6;
}
