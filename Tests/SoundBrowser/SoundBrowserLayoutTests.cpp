// The Sounds view in a column of any height: the footer gives way first (whole strip, one row, nothing), the open
// Filters panel is capped and scrolls inside itself, and no two visible children ever overlap or leave the view.
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>

using namespace sound_browser_test;
using synth::ui::SoundBrowserComponent;
using Mode = SoundBrowserComponent::FooterMode;

namespace {

constexpr int kSearchAndPath = SoundBrowserComponent::kSearchHeight + SoundBrowserComponent::kPathHeight;
constexpr int kMinList = SoundBrowserComponent::kMinListRows * SoundBrowserComponent::kRowHeight;

struct LayoutFixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    LayoutFixture() {
        tree.fill();
        index = makeIndex(tree);
        index->setUserFolders({tree.user});
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setAnimateOffscreenForTest(true);
        view->setSize(300, 900);
        view->showLocation("core", "Drums", 0);
        settle();
    }
    void settle() {
        view->completeFilterMotionForTest();
        view->completeListAnimationForTest();
    }
    void select() {
        for (int i = 0; i < (int)view->getRows().size(); ++i)
            if (view->getRows()[(size_t)i].kind == SoundBrowserComponent::RowKind::Sound) {
                view->selectRow(i);
                return;
            }
    }
    void layout(int width, int height, bool open) {
        view->setSize(width, height);
        view->setFiltersOpen(open, false);
        settle();
    }
};

std::vector<juce::Component*> visibleChildren(juce::Component& parent) {
    std::vector<juce::Component*> out;
    for (auto* c : parent.getChildren())
        if (c->isVisible())
            out.push_back(c);
    return out;
}

} // namespace

// Regression test for FRO992: with the Filters panel open in a short column the footer, the panel, the list and the
// search row all collided. Guards the layout-overlap kind for this view over every height, width and state.
TEST(SoundBrowserLayoutTest, NothingOverlapsAtAnyHeightWidthOrState) {
    int cases = 0;
    LayoutFixture none, picked; // one of each, reused: only the size and the panel change per case
    picked.select();
    for (const int width : {200, 280, 400})
        for (int height = 100; height <= 900; height += 20)
            for (const bool open : {false, true})
                for (const bool selected : {false, true}) {
                    auto& f = selected ? picked : none;
                    auto& v = *f.view;
                    f.layout(width, height, open);
                    const auto where = juce::String("w=") + juce::String(width) + " h=" + juce::String(height) +
                                       (open ? " open" : " closed") + (selected ? " selected" : " none");
                    const auto whole = v.getLocalBounds();
                    const auto kids = visibleChildren(v);
                    for (size_t i = 0; i < kids.size(); ++i) {
                        EXPECT_TRUE(whole.contains(kids[i]->getBounds())) << where << " child " << (int)i;
                        for (size_t j = i + 1; j < kids.size(); ++j)
                            EXPECT_FALSE(kids[i]->getBounds().intersects(kids[j]->getBounds()))
                                << where << " children " << (int)i << " and " << (int)j;
                    }
                    EXPECT_TRUE(v.getSearchField().isVisible() && v.getFiltersButton().isVisible()) << where;
                    EXPECT_GT(v.getSearchField().getWidth(), 0) << where;
                    EXPECT_FALSE(v.getSearchField().getBounds().intersects(v.getFiltersButton().getBounds())) << where;
                    const auto list = v.listArea();
                    if (v.getPreviewFooter().isVisible())
                        EXPECT_FALSE(list.intersects(v.getPreviewFooter().getBounds())) << where;
                    if (v.getPanelViewport().isVisible())
                        EXPECT_FALSE(list.intersects(v.getPanelViewport().getBounds())) << where;
                    EXPECT_GE(v.getListTop(), kSearchAndPath) << where;
                    if (!open && height >= kSearchAndPath + kMinList + SoundBrowserComponent::kCompactFooterHeight)
                        EXPECT_GE(list.getHeight(), kMinList) << where;
                    if (open && height >= kSearchAndPath + synth::ui::SoundFilterPanel::kMinVisibleHeight)
                        EXPECT_GE(v.getPanelViewport().getHeight(), synth::ui::SoundFilterPanel::kMinVisibleHeight)
                            << where << ": an open panel is never squeezed out";
                    if (v.getPreviewFooter().isVisible()) // the footer's own controls stay inside it, apart
                        for (auto* c : visibleChildren(v.getPreviewFooter())) {
                            EXPECT_TRUE(v.getPreviewFooter().getLocalBounds().contains(c->getBounds())) << where;
                            for (auto* d : visibleChildren(v.getPreviewFooter()))
                                if (c != d)
                                    EXPECT_FALSE(c->getBounds().intersects(d->getBounds())) << where;
                        }
                    ++cases;
                }
    EXPECT_EQ(cases, 3 * 41 * 2 * 2);
}

TEST(SoundBrowserLayoutTest, TheFooterGoesCompactThenHiddenAsTheColumnShortens) {
    LayoutFixture f;
    auto& v = *f.view;
    auto& footer = v.getPreviewFooter();
    const int fullFrom = kSearchAndPath + kMinList + SoundBrowserComponent::kFooterHeight;
    const int compactFrom = kSearchAndPath + kMinList + SoundBrowserComponent::kCompactFooterHeight;

    f.layout(280, fullFrom, false);
    EXPECT_EQ(v.getFooterMode(), Mode::Full);
    EXPECT_FALSE(footer.isCompact());
    EXPECT_TRUE(footer.isVisible() && footer.getWaveform().isVisible() && footer.getAutoPlay().isVisible());
    EXPECT_EQ(footer.getHeight(), synth::ui::SoundPreviewFooter::kHeight);

    f.layout(280, fullFrom - 1, false);
    EXPECT_EQ(v.getFooterMode(), Mode::Compact);
    EXPECT_TRUE(footer.isCompact() && footer.isVisible());
    EXPECT_FALSE(footer.getWaveform().isVisible());
    EXPECT_FALSE(footer.getAutoPlay().isVisible());
    EXPECT_TRUE(footer.getStopButton().isVisible() && footer.getVolume().isVisible());
    EXPECT_EQ(footer.getStopButton().getTitle(), "Stop preview");
    EXPECT_EQ(footer.getStopButton().getTooltip(), "Stop the preview (Space)");
    EXPECT_EQ(footer.getVolume().getTitle(), "Preview volume");
    EXPECT_EQ(footer.getVolume().getTooltip(), "Preview volume, only for previews");
    EXPECT_FALSE(footer.getCompactNameBounds().isEmpty());
    EXPECT_EQ(footer.getHeight(), SoundBrowserComponent::kCompactFooterHeight);
    EXPECT_GE(v.listArea().getHeight(), kMinList);

    f.layout(280, compactFrom, false);
    EXPECT_EQ(v.getFooterMode(), Mode::Compact);
    f.layout(280, compactFrom - 1, false);
    EXPECT_EQ(v.getFooterMode(), Mode::Hidden);
    EXPECT_FALSE(footer.isVisible()) << "a hidden footer must be invisible so screen readers skip it";

    f.layout(280, 900, false);
    EXPECT_EQ(v.getFooterMode(), Mode::Full);
    EXPECT_TRUE(footer.getWaveform().isVisible() && footer.getAutoPlay().isVisible());
}

TEST(SoundBrowserLayoutTest, AutoPlayKeepsItsSettingWhileTheFooterIsCompact) {
    LayoutFixture f;
    auto& v = *f.view;
    v.setPreviewSettings(false, -12.0f);
    f.layout(280, 200, false);
    ASSERT_EQ(v.getFooterMode(), Mode::Compact);
    EXPECT_FALSE(v.isAutoPlay());
    EXPECT_FLOAT_EQ(v.getPreviewGainDb(), -12.0f);
    f.layout(280, 900, false);
    EXPECT_FALSE(v.getPreviewFooter().getAutoPlay().getToggleState());
}

TEST(SoundBrowserLayoutTest, ACappedPanelScrollsInsideItselfAndIsNeverEmpty) {
    LayoutFixture f;
    auto& v = *f.view;
    f.layout(280, 400, true);
    auto& viewport = v.getPanelViewport();
    ASSERT_TRUE(viewport.isVisible());
    const int visible = viewport.getMaximumVisibleHeight();
    EXPECT_GT(visible, 40);
    EXPECT_GT(v.getFilterPanel().getHeight(), visible) << "capped: the content is taller than the window on it";
    EXPECT_TRUE(viewport.getVerticalScrollBar().isVisible());
    EXPECT_TRUE(v.getFilterPanel().kindGroup().isVisible());
    EXPECT_TRUE(viewport.getLocalBounds().intersects(
        viewport.getLocalArea(&v.getFilterPanel().kindGroup(), v.getFilterPanel().kindGroup().getLocalBounds())))
        << "the first group is what shows";
    EXPECT_GE(v.listArea().getHeight(), kMinList);

    viewport.setViewPosition(0, 60);
    EXPECT_EQ(viewport.getViewPositionY(), 60) << "the content scrolls";
    EXPECT_LE(v.getFilterPanel().getRight(), viewport.getMaximumVisibleWidth());

    // Tall enough: the whole panel shows and there is nothing to scroll.
    f.layout(280, 900, true);
    EXPECT_EQ(viewport.getHeight(), v.getFilterPanel().getHeight());
    EXPECT_FALSE(viewport.getVerticalScrollBar().isVisible());
}

TEST(SoundBrowserLayoutTest, TheCappedPanelStillAnimatesOpenAndClosed) {
    LayoutFixture f;
    auto& v = *f.view;
    f.layout(280, 400, false);
    const int closedTop = v.getListTop();
    v.setFiltersOpen(true);
    EXPECT_TRUE(v.isFilterMotionRunning());
    EXPECT_LT(v.getPanelViewport().getHeight(), 130) << "it grows, it does not jump to the cap";
    f.settle();
    const int openTop = v.getListTop();
    EXPECT_GT(openTop, closedTop);
    EXPECT_EQ(openTop, closedTop + v.getPanelViewport().getHeight());
    v.setFiltersOpen(false);
    f.settle();
    EXPECT_EQ(v.getListTop(), closedTop);
    EXPECT_FALSE(v.getPanelViewport().isVisible());
}

// Writes the short and default columns, closed and open, in the obsidian theme to SOUND_BROWSER_THEME_STRIP_DIR (when
// set) so the look can be reviewed.
TEST(SoundBrowserLayoutTest, RendersShortAndDefaultColumnsClosedAndOpen) {
    const juce::String dir = juce::SystemStats::getEnvironmentVariable("SOUND_BROWSER_THEME_STRIP_DIR", {});
    synth::theme::AppLookAndFeel laf;
    for (const auto& theme : synth::theme::builtInThemes())
        if (theme.id == "obsidian")
            laf.applyTheme(theme);
    for (const int height : {160, 900})
        for (const bool open : {false, true}) {
            LayoutFixture f;
            f.view->setLookAndFeel(&laf);
            f.select();
            f.layout(280, height, open);
            const auto image = f.view->createComponentSnapshot(f.view->getLocalBounds());
            EXPECT_EQ(image.getWidth(), 280);
            EXPECT_EQ(image.getHeight(), height);
            if (dir.isNotEmpty()) {
                const auto out =
                    juce::File(dir).getChildFile(juce::String(height == 160 ? "short-" : "default-") +
                                                 (open ? "open" : "closed") + "-" + juce::String(height) + ".png");
                out.getParentDirectory().createDirectory();
                out.deleteFile();
                juce::FileOutputStream stream(out);
                juce::PNGImageFormat().writeImageToStream(image, stream);
            }
            f.view->setLookAndFeel(nullptr);
        }
}
