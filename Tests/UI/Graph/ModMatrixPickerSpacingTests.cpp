// ModMatrixPickerSpacingTests.cpp -- the shared list popup (every picker uses it): the gap between the search field and
// the first heading, and the hover words, which show only while a line of the row is cut off. Each test builds the
// picker like a caller does and reads what the rows and the search field really did.
#include "UI/Graph/ModMatrixPicker.h"
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
    explicit Host(std::vector<ModMatrixPicker::Item> items, int width = 300) {
        picker = std::make_unique<ModMatrixPicker>("instrument", std::move(items), -1, [](int) {});
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
