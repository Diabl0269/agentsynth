// The mod dot sources panel's Disconnect all: one icon-only button in the title row, shown from two sources, one
// undo step for the lot.

#include "ModDotTestFixture.h"

#include "UI/Graph/ModDot/ModDotAddSourceParts.h"
#include "UI/Graph/ModDot/ModDotSourceRow.h"
#include "UI/Layout/ReducedMotion.h"

namespace {
struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};
} // namespace

TEST_F(ModuleComponentTest, SourcesDisconnectAllIsHiddenWithOneSourceAndShownWithTwo) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    EXPECT_FALSE(panel->sourcesPage().isDisconnectAllShown());
    f.addSecondLfo();
    f.held.reset();
    panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(panel->sourcesPage().isDisconnectAllShown());
    EXPECT_EQ(panel->sourcesPage().disconnectAllButton().getTooltip(), "Disconnect all connections");
}

TEST_F(ModuleComponentTest, SourcesDisconnectAllRemovesEverySourceAndOneUndoBringsThemBack) {
    NoMotion motion;
    Fixture f;
    f.addSecondLfo();
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    ASSERT_EQ(panel->sourcesPage().rowCount(), 2);
    const auto before = synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel).size();
    ASSERT_EQ(before, 2u);
    panel->sourcesPage().disconnectAllButton().triggerClick();
    synth::test::flushMessageQueue();
    f.refresh();
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel).size(), 0u);
    ASSERT_TRUE(f.undo.canUndo());
    f.undo.undo();
    f.refresh();
    EXPECT_EQ(synth::ui::knobModSources(*f.editor, f.vcaId, f.gainChannel).size(), 2u) << "one undo restores both";
}
