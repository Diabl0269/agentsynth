// The Filters button, panel and chip row of the Sounds view on the real components: Space and Esc, the panel pushing
// the list down, chips staying in step with the panel in both directions, Clear all, and the list narrowing.
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include <gtest/gtest.h>

using namespace sound_browser_test;
using synth::soundbrowser::SoundKind;
using synth::ui::SoundBrowserComponent;
using synth::ui::SoundFilterChipRow;

namespace {

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }
juce::KeyPress space() { return juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(), ' '); }

juce::MouseEvent clickAt(juce::Component& target, juce::Point<int> local) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local.toFloat(), {}, 0.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, &target, &target, juce::Time::getCurrentTime(), local.toFloat(),
                            juce::Time::getCurrentTime(), 1, false);
}

int soundRows(const SoundBrowserComponent& v) {
    int n = 0;
    for (const auto& r : v.getRows())
        n += r.kind == SoundBrowserComponent::RowKind::Sound ? 1 : 0;
    return n;
}

struct Fixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    Fixture() {
        tree.fill();
        index = makeIndex(tree);
        index->setUserFolders({tree.user});
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 900);
        view->setAnimateOffscreenForTest(true);
    }
    void settle() {
        view->completeFilterMotionForTest();
        view->completeListAnimationForTest();
    }
};

} // namespace

TEST(SoundFiltersViewTest, SpaceOpensAndEscClosesThePanelAndTheButtonSaysSo) {
    Fixture f;
    auto& v = *f.view;
    auto& button = v.getFiltersButton();
    EXPECT_EQ(button.getButtonText(), "Filters");
    EXPECT_EQ(button.getTitle(), "Filters, 0 on, collapsed");
    EXPECT_EQ(button.getTooltip(), "Narrow by kind, sound, tempo, key, length or place");
    EXPECT_FALSE(v.areFiltersOpen());

    EXPECT_TRUE(button.keyPressed(space()));
    f.settle();
    EXPECT_TRUE(v.areFiltersOpen());
    EXPECT_EQ(button.getTitle(), "Filters, 0 on, expanded");

    EXPECT_TRUE(v.getFilterPanel().keyPressed(key(juce::KeyPress::escapeKey)));
    f.settle();
    EXPECT_FALSE(v.areFiltersOpen());
    EXPECT_TRUE(button.keyPressed(key(juce::KeyPress::returnKey)));
    f.settle();
    EXPECT_TRUE(v.areFiltersOpen());
    EXPECT_TRUE(button.keyPressed(space()));
    f.settle();
    EXPECT_FALSE(v.areFiltersOpen());
}

TEST(SoundFiltersViewTest, EscFromAChildThatBubblesUpClosesThePanelToo) {
    Fixture f;
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    v.getFilterPanel().kindGroup().grabKeyboardFocus();
    // Headless windows give no real focus: the panel's own handler is the path that is testable here.
    EXPECT_TRUE(v.getFilterPanel().keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(v.areFiltersOpen());
}

TEST(SoundFiltersViewTest, TheOpenPanelPushesTheListDownAndNeverCoversIt) {
    Fixture f;
    auto& v = *f.view;
    const int closedTop = v.getListTop();
    EXPECT_EQ(closedTop, SoundBrowserComponent::kSearchHeight + SoundBrowserComponent::kPathHeight);
    v.setFiltersOpen(true);
    EXPECT_TRUE(v.isFilterMotionRunning()) << "the panel grows open, it does not jump";
    f.settle();
    const int panelHeight = v.getPanelViewport().getHeight();
    ASSERT_GT(panelHeight, 100);
    EXPECT_EQ(v.getListTop(), closedTop + panelHeight);
    // The panel ends where the path bar begins and the list begins one path bar later.
    const auto host = v.getPanelViewport().getBounds();
    EXPECT_EQ(host.getBottom() + SoundBrowserComponent::kPathHeight, v.getListTop());
    EXPECT_FALSE(host.intersects(v.getRowBounds(0)));

    v.setFiltersOpen(false);
    f.settle();
    EXPECT_EQ(v.getListTop(), closedTop);
}

TEST(SoundFiltersViewTest, ChipsFollowThePanelAndTheButtonCountsThem) {
    Fixture f;
    auto& v = *f.view;
    v.setFiltersOpen(true, false);
    f.settle();
    auto& kind = v.getFilterPanel().kindGroup();
    kind.setFocusedIndex(1);
    EXPECT_TRUE(kind.keyPressed(space()));
    f.settle();
    EXPECT_TRUE(v.getFilters().hasKind(SoundKind::Loop));
    EXPECT_FALSE(v.getChipRow().getChipBounds("kind:1").isEmpty());
    EXPECT_EQ(v.getFiltersButton().getButtonText(), "Filters (1)");
    EXPECT_EQ(v.getFiltersButton().getTitle(), "Filters, 1 on, expanded");

    v.getFilterPanel()
        .tempoSlider()
        .getHandle(synth::ui::RangeSlider::Handle::Lower)
        .keyPressed(key(juce::KeyPress::rightKey)); // 61-200 BPM; the full range would mean no filter
    f.settle();
    EXPECT_TRUE(v.getFilters().tempoRange().has_value());
    EXPECT_FALSE(v.getChipRow().getChipBounds("tempo").isEmpty());
    EXPECT_EQ(v.getFiltersButton().getButtonText(), "Filters (2)");
}

TEST(SoundFiltersViewTest, RemovingAChipSwitchesItOffInThePanelAndClearAllEmptiesBoth) {
    Fixture f;
    auto& v = *f.view;
    v.getFilters().setKind(SoundKind::Loop, true);
    v.getFilters().setTag("Drums", true);
    v.applyFiltersChanged();
    f.settle();
    auto& row = v.getChipRow();
    ASSERT_FALSE(row.getChipBounds("kind:1").isEmpty());
    ASSERT_FALSE(row.getChipBounds("tag:Drums").isEmpty());
    EXPECT_TRUE(v.getFilterPanel().kindGroup().isOn("1"));
    EXPECT_EQ(row.getChipBounds("kind:1").getHeight(), SoundFilterChipRow::kHeight - 6) << "small chips";

    row.mouseDown(clickAt(row, row.getRemoveBounds("kind:1").getCentre()));
    f.settle();
    EXPECT_FALSE(v.getFilters().hasKind(SoundKind::Loop));
    EXPECT_FALSE(v.getFilterPanel().kindGroup().isOn("1")) << "the panel's chip went off with it";
    EXPECT_TRUE(row.getChipBounds("kind:1").isEmpty());
    EXPECT_TRUE(v.getFilterPanel().soundGroup().isOn("Drums"));

    row.mouseDown(clickAt(row, row.getChipBounds(SoundFilterChipRow::kClearAllId).getCentre()));
    f.settle();
    EXPECT_EQ(v.getFilters().count(), 0);
    EXPECT_FALSE(v.getFilterPanel().soundGroup().isOn("Drums"));
    EXPECT_EQ(v.getFiltersButton().getButtonText(), "Filters");
    EXPECT_EQ(row.getHeight(), 0) << "the row goes when the last chip does";
}

TEST(SoundFiltersViewTest, ChipRowKeysMoveAndRemove) {
    Fixture f;
    auto& v = *f.view;
    v.getFilters().setKind(SoundKind::Loop, true);
    v.getFilters().setTag("Keys", true);
    v.applyFiltersChanged();
    f.settle();
    auto& row = v.getChipRow();
    EXPECT_EQ(row.getFocusedId(), "kind:1");
    EXPECT_TRUE(row.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(row.getFocusedId(), "tag:Keys");
    EXPECT_TRUE(row.keyPressed(key(juce::KeyPress::deleteKey)));
    f.settle();
    EXPECT_FALSE(v.getFilters().hasTag("Keys"));
    EXPECT_TRUE(v.getFilters().hasKind(SoundKind::Loop));
}

TEST(SoundFiltersViewTest, ChipsGrowInAndShrinkOutOverTime) {
    Fixture f;
    auto& v = *f.view;
    v.getFilters().setKind(SoundKind::Loop, true);
    v.applyFiltersChanged();
    EXPECT_TRUE(v.getChipRow().isMotionRunning());
    EXPECT_LT(v.getChipRow().rowPresence(), 1.0f) << "a new chip is not full size on its first frame";
    f.settle();
    EXPECT_FLOAT_EQ(v.getChipRow().rowPresence(), 1.0f);
}

TEST(SoundFiltersViewTest, TheListNarrowsToWhatPassesAndSaysWhenNothingDoes) {
    Fixture f;
    auto& v = *f.view;
    v.getFilters().setKind(SoundKind::Loop, true);
    v.applyFiltersChanged();
    f.settle();
    ASSERT_EQ(soundRows(v), 1);
    EXPECT_EQ(v.getRows()[0].text, "dusty_keys_120_Am_loop");

    v.getFilters().setTempoRange(std::make_pair(60, 90));
    v.applyFiltersChanged();
    f.settle();
    EXPECT_EQ(soundRows(v), 0);
    ASSERT_EQ(v.getRows().size(), 1u);
    EXPECT_EQ(v.getRows()[0].kind, SoundBrowserComponent::RowKind::Hint);

    v.getFilters().setTempoRange(std::make_pair(100, 130));
    v.getFilters().setPickedKey("A minor");
    v.applyFiltersChanged();
    f.settle();
    EXPECT_EQ(soundRows(v), 1);
    v.getFilters().setPickedKey("C");
    v.applyFiltersChanged();
    f.settle();
    EXPECT_EQ(soundRows(v), 0);

    v.getFilters().clearAll();
    v.applyFiltersChanged();
    f.settle();
    EXPECT_GE(v.getRows().size(), 2u) << "the Places screen is back";

    // Inside a place the filter applies to the folder listing too.
    v.showLocation("core", "Drums", 0);
    f.settle();
    const int all = (int)v.getRows().size();
    v.getFilters().setLength(synth::soundbrowser::SoundLength::Under1);
    v.applyFiltersChanged();
    f.settle();
    EXPECT_LT((int)v.getRows().size(), all) << "the 1.2 s kick folder is gone, the 0.3 s snare stays";
    EXPECT_EQ(soundRows(v), 1);
}

TEST(SoundFiltersViewTest, NearSongTempoUsesTheTransportTempo) {
    Fixture f;
    auto& v = *f.view;
    double bpm = 120.0;
    v.songTempoProvider = [&bpm] { return bpm; };
    v.getFilters().setNearSongTempo(true);
    v.applyFiltersChanged();
    f.settle();
    EXPECT_EQ(soundRows(v), 1);
    bpm = 150.0;
    v.applyFiltersChanged();
    f.settle();
    EXPECT_EQ(soundRows(v), 0);
    bpm = 240.0; // the loop is 120, half of 240
    v.applyFiltersChanged();
    f.settle();
    EXPECT_EQ(soundRows(v), 1);
}
