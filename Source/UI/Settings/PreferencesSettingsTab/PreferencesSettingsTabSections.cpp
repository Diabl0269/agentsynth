#include "PreferencesSettingsTab.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Settings/ShortcutsSettingsTab.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

// Concern: the "All" category view. Every category sits under a clickable header (chevron + name)
// that folds its rows, and the one fold-all button acts on every header at once. The rows
// themselves are laid out by the usual layout*Groups units; this unit only decides which categories
// contribute rows (categoryShown) and, once the pass is done, slots each header above its rows.

namespace {
constexpr int kHeaderHeight = 26;
constexpr int kHeaderGap = 4;          // between a header and its first row
constexpr int kSectionGap = 8;         // above every header but the first
constexpr int kDividerLeadHeight = 21; // the 10 + 1 + 10 a group divider occupies (see layoutContent)
constexpr int kChevronSize = 8;
constexpr float kHeaderTextAlpha = 0.85f;
constexpr float kHeaderHoverTextAlpha = 1.0f;
constexpr float kHeaderRuleAlpha = 0.12f;

constexpr PreferencesSettingsTab::Category kSections[] = {
    PreferencesSettingsTab::Category::Graph,  PreferencesSettingsTab::Category::Timeline,
    PreferencesSettingsTab::Category::Files,  PreferencesSettingsTab::Category::Mixer,
    PreferencesSettingsTab::Category::Panels, PreferencesSettingsTab::Category::MidiRemote};

int indexOf(PreferencesSettingsTab::Category c) { return static_cast<int>(c); }
} // namespace

PreferencesSettingsTab::SectionHeader::SectionHeader(PreferencesSettingsTab& o, Category c)
    : juce::Button(categoryName(c))
    , owner(o)
    , category(c) {
    setWantsKeyboardFocus(true);
    refreshTitle();
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    onClick = [this] { owner.setSectionCollapsed(category, !owner.isSectionCollapsed(category)); };
}

bool PreferencesSettingsTab::SectionHeader::isFolded() const { return owner.isSectionCollapsed(category); }

void PreferencesSettingsTab::SectionHeader::setFolded(bool folded) { owner.setSectionCollapsed(category, folded); }

// The accessible name carries the state ("Graph, expanded"), so a screen reader hears it change.
void PreferencesSettingsTab::SectionHeader::refreshTitle() {
    setTitle(categoryName(category) + (owner.isSectionCollapsed(category) ? ", collapsed" : ", expanded"));
}

void PreferencesSettingsTab::SectionHeader::paintButton(juce::Graphics& g, bool hot, bool /*down*/) {
    const auto textColour = findColour(juce::Label::textColourId);
    const auto colour = textColour.withAlpha(hot || hasKeyboardFocus(false) ? kHeaderHoverTextAlpha : kHeaderTextAlpha);
    const auto bounds = getLocalBounds();
    synth::theme::paintDisclosureChevron(g,
                                         juce::Rectangle<float>(6.0f, (float)(bounds.getHeight() - kChevronSize) * 0.5f,
                                                                (float)kChevronSize, (float)kChevronSize),
                                         owner.isSectionCollapsed(category) ? 0.0f : 1.0f, synth::theme::themeOf(*this),
                                         hot || hasKeyboardFocus(false));
    g.setColour(colour);
    g.setFont(juce::Font(juce::FontOptions(12.5f, juce::Font::bold)));
    g.drawText(categoryName(category), bounds.withTrimmedLeft(22), juce::Justification::centredLeft);
    g.setColour(textColour.withAlpha(kHeaderRuleAlpha));
    g.fillRect(0, bounds.getBottom() - 1, bounds.getWidth(), 1);
    synth::ui::paintFocusRing(g, bounds.toFloat().reduced(1.0f), *this);
}

void PreferencesSettingsTab::setupSectionControls() {
    for (auto category : kSections) {
        auto& header = sectionHeaders[indexOf(category)];
        header = std::make_unique<SectionHeader>(*this, category);
        header->setTooltip("Fold or unfold " + categoryName(category) + " (Left/Right)");
        contentHost.addChildComponent(*header);
    }
    addChildComponent(foldAllButton);
    foldAllButton.onClick = [this] { setAllSectionsCollapsed(!areAllSectionsCollapsed()); };
}

void PreferencesSettingsTab::refreshSectionTitles() {
    for (auto& header : sectionHeaders)
        if (header != nullptr)
            header->refreshTitle();
}

bool PreferencesSettingsTab::isSectionCollapsed(Category category) const {
    return category != Category::All && sectionCollapsed[indexOf(category)];
}

void PreferencesSettingsTab::setSectionCollapsed(Category category, bool collapsed) {
    if (category == Category::All || sectionCollapsed[indexOf(category)] == collapsed)
        return;
    sectionCollapsed[indexOf(category)] = collapsed;
    refreshSectionTitles();
    resized();
    repaint();
}

bool PreferencesSettingsTab::areAllSectionsCollapsed() const {
    for (auto flag : sectionCollapsed)
        if (!flag)
            return false;
    return true;
}

void PreferencesSettingsTab::setAllSectionsCollapsed(bool collapsed) {
    for (auto& flag : sectionCollapsed)
        flag = collapsed;
    refreshSectionTitles();
    resized();
    repaint();
}

juce::Button& PreferencesSettingsTab::getSectionHeaderForTest(Category category) {
    return *sectionHeaders[indexOf(category)];
}

// With no filter: the picked category only, or (All) every category that is not folded.
bool PreferencesSettingsTab::categoryShown(Category category) const {
    if (selectedCategory == Category::All)
        return !isSectionCollapsed(category);
    return category == selectedCategory;
}

// Called first by every layout*Groups unit (Files is entered twice: only the first counts).
void PreferencesSettingsTab::enterCategory(Category category, int y) {
    layoutCategory = category;
    auto& start = sectionStartY[indexOf(category)];
    if (start < 0)
        start = y;
}

// After layoutContent: each category's rows occupy a band [start, nextStart) of the content. Walk the
// bands in order, put a header where each begins, drop the group divider that would sit directly under
// a header, and shift the band's rows and dividers down to make room. Positions are snapshotted first
// so a shifted row is never mistaken for a member of the next band.
void PreferencesSettingsTab::placeSectionHeaders(int contentWidth) {
    const bool active = sectionHeadersActive();
    for (auto& header : sectionHeaders)
        if (header != nullptr)
            header->setVisible(active);
    if (!active)
        return;

    struct Placed {
        juce::Component* component;
        int oldY;
    };
    std::vector<Placed> rows;
    for (auto* child : contentHost.getChildren()) {
        bool isHeader = false;
        for (auto& header : sectionHeaders)
            isHeader = isHeader || child == header.get();
        if (!isHeader && child->isVisible())
            rows.push_back({child, child->getY()});
    }
    const auto oldDividers = dividerBounds;
    dividerBounds.clear();

    const int totalHeight = contentHost.getHeight();
    int cursor = 0;
    for (int i = 0; i < kNumSections; ++i) {
        const int bandTop = sectionStartY[i];
        int bandEnd = totalHeight;
        for (int j = i + 1; j < kNumSections; ++j)
            if (sectionStartY[j] >= 0) {
                bandEnd = sectionStartY[j];
                break;
            }
        if (bandTop < 0)
            continue;

        if (i > 0)
            cursor += kSectionGap;
        sectionHeaders[i]->setBounds(0, cursor, contentWidth, kHeaderHeight);
        cursor += kHeaderHeight;
        if (bandEnd <= bandTop)
            continue; // folded: the header stands alone

        // The divider between the previous visible group and this band's first row would now sit
        // right under the header, so the band's content starts after it.
        int contentOrigin = bandTop;
        for (const auto& d : oldDividers)
            if (d.getY() == bandTop + 10)
                contentOrigin = bandTop + kDividerLeadHeight;
        cursor += kHeaderGap;
        const int shift = cursor - contentOrigin;
        for (const auto& r : rows)
            if (r.oldY >= contentOrigin && r.oldY < bandEnd)
                r.component->setTopLeftPosition(r.component->getX(), r.oldY + shift);
        for (const auto& d : oldDividers)
            if (d.getY() >= contentOrigin && d.getY() < bandEnd)
                dividerBounds.push_back(d.translated(0, shift));
        cursor += bandEnd - contentOrigin;
    }
    contentHost.setSize(contentHost.getWidth(), juce::jmax(cursor, 1));
    contentHost.repaint();
}
