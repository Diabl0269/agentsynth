// The Sounds view's preview footer at every column width and footer mode: the empty waveform says what to do, and no
// two footer controls overlap or leave the footer from the narrowest column (200 px) to the widest (400 px).
#include "SoundBrowserTestKit.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <set>
#include <utility>

using namespace sound_browser_test;
using synth::ui::LibrarySidebarViews;
using synth::ui::SoundBrowserComponent;
using synth::ui::SoundPreviewFooter;
using Mode = SoundBrowserComponent::FooterMode;

namespace {

constexpr int kSearchAndPath = SoundBrowserComponent::kSearchHeight + SoundBrowserComponent::kPathHeight;
constexpr int kMinList = SoundBrowserComponent::kMinListRows * SoundBrowserComponent::kRowHeight;

std::vector<juce::Component*> visibleChildren(juce::Component& parent) {
    std::vector<juce::Component*> out;
    for (auto* c : parent.getChildren())
        if (c->isVisible())
            out.push_back(c);
    return out;
}

// Every visible control lies inside `footer` and touches no other .
void expectControlsApart(SoundPreviewFooter& footer, const juce::String& where) {
    const auto kids = visibleChildren(footer);
    for (size_t i = 0; i < kids.size(); ++i) {
        EXPECT_TRUE(footer.getLocalBounds().contains(kids[i]->getBounds())) << where << " child " << (int)i;
        for (size_t j = i + 1; j < kids.size(); ++j)
            EXPECT_FALSE(kids[i]->getBounds().intersects(kids[j]->getBounds()))
                << where << " children " << (int)i << " and " << (int)j;
    }
}

int countNear(const juce::Image& image, juce::Colour colour) {
    int n = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x) {
            const auto p = image.getPixelAt(x, y);
            const auto near = [](juce::uint8 a, juce::uint8 b) { return std::abs((int)a - (int)b) <= 12; };
            n += p.getAlpha() == 255 && near(p.getRed(), colour.getRed()) && near(p.getGreen(), colour.getGreen()) &&
                         near(p.getBlue(), colour.getBlue())
                     ? 1
                     : 0;
        }
    return n;
}

} // namespace

// Regression test for FRO1388: the empty waveform box was blank.
TEST(SoundPreviewFooterLayoutTest, TheEmptyWaveformBoxSaysToSelectASoundAndOnlyWhileNoFileIsSet) {
    synth::theme::AppLookAndFeel laf;
    for (const auto& theme : synth::theme::builtInThemes())
        if (theme.id == "obsidian")
            laf.applyTheme(theme);
    const auto muted = laf.getTheme().colors.textMuted;
    TempTree tree;
    const auto wav = tree.dir.getChildFile("tone.wav");
    writeWav(wav, 0.5);

    synth::ui::SoundWaveformView wave;
    wave.setLookAndFeel(&laf);
    wave.setSize(200, 40);
    const auto render = [&wave] {
        juce::Image image(juce::Image::ARGB, wave.getWidth(), wave.getHeight(), true, juce::SoftwareImageType());
        juce::Graphics g(image);
        wave.paintEntireComponent(g, true);
        return image;
    };
    EXPECT_GT(countNear(render(), muted), 8) << "the hint's muted text shows with no file";

    wave.setFile(wav);
    wave.finishRevealForTest();
    EXPECT_EQ(countNear(render(), muted), 0) << "no hint once a sound is shown";
    wave.setFile(juce::File());
    EXPECT_GT(countNear(render(), muted), 8) << "and it returns when the selection clears";

    wave.setSize(60, 40); // narrower than the sentence: it is cut with an ellipsis inside the box
    const auto narrow = render();
    EXPECT_GT(countNear(narrow, muted), 3);
    for (int y = 0; y < narrow.getHeight(); ++y) // nothing muted lands in the 3 px border margin
        for (const int x : {0, 1, 2, narrow.getWidth() - 1, narrow.getWidth() - 2, narrow.getWidth() - 3})
            EXPECT_NE(juce::Colour(narrow.getPixelAt(x, y)).getARGB(), muted.getARGB());
    wave.setLookAndFeel(nullptr);
}

// Regression test for FRO1388: guards layout-overlap and row-break for the footer. At every width from 200 to 400
// px, step 8, in the full and the compact footer, nothing overlaps and everything stays inside. In the full footer the
// two labelled switches (80 px each) and Play (56 px) cannot share a row below 240 px of inner width, so under it the
// switches take a row and Play shares the next with the volume; the row count never grows past two.
TEST(SoundPreviewFooterLayoutTest, NoFooterControlsOverlapAtAnyWidthInEitherLayout) {
    int cases = 0;
    for (int width = LibrarySidebarViews::kMinSoundsWidth; width <= 400; width += 8)
        for (const bool compact : {false, true}) {
            SoundPreviewFooter footer;
            footer.setCompact(compact);
            footer.setSize(width, compact ? SoundPreviewFooter::kCompactHeight : SoundPreviewFooter::kHeight);
            const auto where = juce::String("w=") + juce::String(width) + (compact ? " compact" : " full");
            expectControlsApart(footer, where);
            EXPECT_GE(footer.getPlayButton().getWidth(), 56) << where;
            EXPECT_GE(footer.getVolume().getWidth(), 60) << where;
            if (!compact) {
                EXPECT_GE(footer.getAutoPlay().getWidth(), 80) << where;
                EXPECT_GE(footer.getMatchKey().getWidth(), 80) << where;
                std::set<int> rows;
                for (auto* c : {static_cast<juce::Component*>(&footer.getPlayButton()),
                                static_cast<juce::Component*>(&footer.getAutoPlay()),
                                static_cast<juce::Component*>(&footer.getMatchKey()),
                                static_cast<juce::Component*>(&footer.getVolume())})
                    rows.insert(c->getY());
                EXPECT_LE(rows.size(), 2u) << where << ": the controls take two rows at most";
            }
            ++cases;
        }
    EXPECT_EQ(cases, 2 * 26);
}

// Dragging the divider narrower, then the column shorter, through the real view: each footer mode keeps its controls
// apart and inside, and the footer hides rather than squeezing at short heights.
TEST(SoundPreviewFooterLayoutTest, ThroughTheViewEveryWidthAndFooterModeKeepsTheControlsApart) {
    TempTree tree;
    tree.fill();
    synth::soundbrowser::SoundLists lists;
    auto index = makeIndex(tree);
    index->setUserFolders({tree.user});
    scanAndWait(*index);
    SoundBrowserComponent view(*index, lists);
    view.setAnimateOffscreenForTest(true);
    view.showLocation("core", "Drums", 0);
    view.completeListAnimationForTest();
    const int fullFrom = kSearchAndPath + kMinList + SoundBrowserComponent::kFooterHeight;
    const int compactFrom = kSearchAndPath + kMinList + SoundBrowserComponent::kCompactFooterHeight;
    int shown = 0;
    for (int width = LibrarySidebarViews::kMinSoundsWidth; width <= 400; width += 8)
        for (const auto [mode, height] :
             {std::pair{Mode::Full, fullFrom}, std::pair{Mode::Compact, compactFrom}, std::pair{Mode::Hidden, 120}}) {
            view.setSize(width, height);
            view.completeFilterMotionForTest();
            const auto where = juce::String("w=") + juce::String(width) + " h=" + juce::String(height);
            ASSERT_EQ(view.getFooterMode(), mode) << where;
            auto& footer = view.getPreviewFooter();
            if (mode == Mode::Hidden) {
                EXPECT_FALSE(footer.isVisible()) << where;
                continue;
            }
            EXPECT_TRUE(view.getLocalBounds().contains(footer.getBounds())) << where;
            expectControlsApart(footer, where);
            ++shown;
        }
    EXPECT_EQ(shown, 2 * 26);
}
