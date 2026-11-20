// ModMatrixPickerSpacingTests.cpp -- the shared list popup (every picker uses it): the gap between the search field and
// the first heading, and the hover words, which show only while a line of the row is cut off. Each test builds the
// picker like a caller does and reads what the rows and the search field really did.
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>
#include <memory>

using synth::ui::ModMatrixPicker;

namespace {

ModMatrixPicker::Item item(int id, const juce::String& text, const juce::String& detail = {}) {
    ModMatrixPicker::Item i;
    i.id = id;
    i.category = "Built in";
    i.text = text;
    i.detail = detail;
    return i;
}

struct Host {
    juce::Component parent;
    std::unique_ptr<ModMatrixPicker> picker;
    ~Host() { parent.setLookAndFeel(nullptr); }
    explicit Host(std::vector<ModMatrixPicker::Item> items, int width = 300, const juce::String& what = "instrument",
                  juce::LookAndFeel* lookAndFeel = nullptr) {
        if (lookAndFeel != nullptr)
            parent.setLookAndFeel(lookAndFeel);
        picker = std::make_unique<ModMatrixPicker>(what, std::move(items), -1, [](int) {});
        parent.setSize(400, 500);
        parent.addAndMakeVisible(*picker);
        picker->setBounds(0, 0, width, 300);
    }
    // The row components in the list, top first.
    std::vector<juce::Component*> rows() const {
        std::vector<juce::Component*> found;
        for (auto* child : picker->getRowColumnForTest().getChildren())
            if (child->isVisible())
                found.push_back(child);
        std::sort(found.begin(), found.end(), [](auto* a, auto* b) { return a->getY() < b->getY(); });
        return found;
    }
    juce::Component* row(const juce::String& title) const {
        for (auto* r : rows())
            if (r->getTitle() == title)
                return r;
        return nullptr;
    }
    static juce::String tip(juce::Component* c) {
        auto* client = dynamic_cast<juce::TooltipClient*>(c);
        return client != nullptr ? client->getTooltip() : juce::String();
    }
};

const juce::String kLong = "A very long instrument name that cannot possibly fit on one line of the list";

} // namespace

// Regression test for FRO2020: the first heading sat 4 px under the search field and read as touching it.
TEST(ModMatrixPickerSpacing, TheFirstHeadingHasRoomUnderTheSearchField) {
    Host h({item(1, "French horn"), item(2, "Cello")});
    const auto rows = h.rows();
    ASSERT_FALSE(rows.empty());
    const int searchBottom = h.picker->getSearchEditorForTest().getBottom();
    const int listTop = h.picker->getLocalArea(rows.front(), rows.front()->getLocalBounds()).getY();
    EXPECT_GE(listTop - searchBottom, 8) << "the list starts at least 8 px under the search field";
}

TEST(ModMatrixPickerSpacing, TheSameGapHoldsForAListWithNoHeadings) {
    auto flat = item(1, "French horn");
    flat.category = {};
    Host h({flat});
    const auto rows = h.rows();
    ASSERT_FALSE(rows.empty());
    const int searchBottom = h.picker->getSearchEditorForTest().getBottom();
    EXPECT_GE(h.picker->getLocalArea(rows.front(), rows.front()->getLocalBounds()).getY() - searchBottom, 8);
}

// Regression test for FRO2020: every row's tooltip repeated its visible text.
TEST(ModMatrixPickerTooltip, AShortRowHasNoTooltipButKeepsItsAccessibleName) {
    Host h({item(1, "Cello", "Starter instruments"), item(2, "Oboe")});
    auto* cello = h.row("Cello, Starter instruments");
    ASSERT_NE(cello, nullptr);
    EXPECT_TRUE(Host::tip(cello).isEmpty());
    auto* oboe = h.row("Oboe");
    ASSERT_NE(oboe, nullptr);
    EXPECT_TRUE(Host::tip(oboe).isEmpty());
}

TEST(ModMatrixPickerTooltip, ARowWhoseNameIsCutOffExplainsItself) {
    Host h({item(1, kLong, "Starter instruments"), item(2, "Oboe")});
    auto* cut = h.row(kLong + ", Starter instruments");
    ASSERT_NE(cut, nullptr);
    EXPECT_EQ(Host::tip(cut), kLong + ", Starter instruments");
    EXPECT_TRUE(Host::tip(h.row("Oboe")).isEmpty());
}

TEST(ModMatrixPickerTooltip, ARowWhoseOnlyDetailIsCutOffKeepsItsTooltip) {
    Host h({item(1, "Cello", "Starter instruments from the sound pack that carries a long name of its own, and more")});
    const auto title = "Cello, Starter instruments from the sound pack that carries a long name of its own, and more";
    auto* row = h.row(title);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(Host::tip(row), title);
}

TEST(ModMatrixPickerTooltip, TheTooltipFollowsTheRowWhenThePopupIsResized) {
    Host h({item(1, "French horn section", "Starter")}, 300);
    const juce::String title = "French horn section, Starter";
    ASSERT_NE(h.row(title), nullptr);
    EXPECT_TRUE(Host::tip(h.row(title)).isEmpty());

    h.picker->setBounds(0, 0, 90, 300);
    ASSERT_NE(h.row(title), nullptr);
    EXPECT_EQ(Host::tip(h.row(title)), title) << "narrowed until the name is cut: the tooltip appears";

    h.picker->setBounds(0, 0, 300, 300);
    EXPECT_TRUE(Host::tip(h.row(title)).isEmpty()) << "widened again: it goes";
}

TEST(ModMatrixPickerTooltip, ATickedRowCountsItsGutterWhenDecidingWhatIsCut) {
    // Fits with no gutter at 150 px wide but not once a tick leaves room for itself.
    const juce::String name = "Plucked double bass line";
    auto plain = item(1, name);
    Host without({plain}, 150);
    auto ticked = plain;
    ticked.ticked = true;
    Host with({ticked}, 150);
    EXPECT_TRUE(Host::tip(without.row(name)).isEmpty() || !Host::tip(with.row(name)).isEmpty())
        << "a tick never removes a tooltip";
}

namespace {

// The columns of one pixel row that differ from `base`, as runs of adjacent columns [first, last].
std::vector<std::pair<int, int>> inkRuns(const juce::Image& img, int y, int fromX, juce::Colour base) {
    std::vector<std::pair<int, int>> runs;
    for (int x = fromX; x < img.getWidth(); ++x) {
        const auto c = img.getPixelAt(x, y);
        const int diff = std::abs(c.getRed() - base.getRed()) + std::abs(c.getGreen() - base.getGreen()) +
                         std::abs(c.getBlue() - base.getBlue());
        if (diff <= 60)
            continue;
        if (!runs.empty() && runs.back().second == x - 1)
            runs.back().second = x;
        else
            runs.emplace_back(x, x);
    }
    return runs;
}

// Paints a highlighted, ticked row of a picker of kind `what` and returns {gap from the highlight's left border to the
// tick, gap from the tick to the name}, both in px; {-1, -1} when the pixels do not show them.
std::pair<int, int> tickGaps(const juce::String& what, synth::theme::AppLookAndFeel& lookAndFeel) {
    auto ticked = item(1, "Cello");
    ticked.ticked = true;
    Host h({ticked, item(2, "Oboe")}, 300, what, &lookAndFeel);
    auto* row = h.row("Cello");
    if (row == nullptr)
        return {-1, -1};
    if (h.picker->getHighlightedItemIndexForTest() != 0) // the highlight sits on the ticked row
        h.picker->sendKeyForTest(juce::KeyPress(juce::KeyPress::upKey));
    if (h.picker->getHighlightedItemIndexForTest() != 0)
        return {-2, -2};
    juce::Image img(juce::Image::ARGB, row->getWidth(), row->getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(img);
    const auto backdrop = lookAndFeel.getTheme().colors.surface.withAlpha(1.0f);
    g.fillAll(backdrop);
    row->paintEntireComponent(g, false);
    const int y = row->getHeight() / 2;
    // The highlight's left border: the first column that is not the panel colour behind it.
    int highlightLeft = -1;
    for (int x = 0; x < img.getWidth() && highlightLeft < 0; ++x)
        if (img.getPixelAt(x, y) != backdrop)
            highlightLeft = x;
    if (highlightLeft < 0)
        return {-1, -1};
    // Ink is whatever differs from the fill a few px inside the highlight, on any line of the row.
    const auto fill = img.getPixelAt(highlightLeft + 1, y);
    // The columns that carry ink on any line: the tick is the first run of them, the name the second.
    std::vector<bool> inked((size_t)img.getWidth(), false);
    for (int yy = 4; yy < img.getHeight() - 4; ++yy) // inside the highlight, clear of its rounded ends
        for (const auto& run : inkRuns(img, yy, highlightLeft + 1, fill))
            for (int x = run.first; x <= run.second; ++x)
                inked[(size_t)x] = true;
    std::vector<std::pair<int, int>> columns;
    for (int x = 0; x < img.getWidth(); ++x) {
        if (!inked[(size_t)x])
            continue;
        if (!columns.empty() && columns.back().second == x - 1)
            columns.back().second = x;
        else
            columns.emplace_back(x, x);
    }
    if (columns.size() < 2)
        return {-1, -static_cast<int>(columns.size()) - 10 * highlightLeft};
    const int tickLeft = columns[0].first;
    const int tickRight = columns[0].second;
    const int textLeft = columns[1].first;
    return {tickLeft - highlightLeft, textLeft - tickRight - 1};
}

} // namespace

// Regression test for FRO2131: the tick sat 17 px from the highlight's left border but 7 px from the name.
TEST(ModMatrixPickerSpacing, ATickIsAsFarFromTheHighlightBorderAsFromTheName) {
    synth::theme::ThemeManager themes;
    for (const char* themeId : {"neon", "daylight"}) {
        ASSERT_TRUE(themes.setActiveTheme(themeId));
        synth::theme::AppLookAndFeel lf;
        lf.applyTheme(themes.getActiveTheme());
        // Every picker is the same list; its kind only changes the words.
        for (const char* what : {"source", "automation", "instrument"}) {
            const auto [fromBorder, toName] = tickGaps(what, lf);
            ASSERT_GE(fromBorder, 0) << themeId << " " << what << " " << toName;
            EXPECT_LE(std::abs(fromBorder - toName), 1) << themeId << " " << what << ": " << fromBorder
                                                        << " px from the border, " << toName << " px to the name";
        }
    }
}
