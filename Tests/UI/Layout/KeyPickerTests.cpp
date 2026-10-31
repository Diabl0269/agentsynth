// KeyPickerTests.cpp
//
// The key picker: a search field that understands "d dor", a keyboard strip, a grouped scale list, and the
// Relative / Detect / No key / Follow actions. Opened headless (no CallOutBox): a pick reports through onPick.

#include "../../TestWait.h"
#include "Music/SongKey.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/Layout/SearchMatch.h"
#include <gtest/gtest.h>

namespace {

using synth::MusicalScale;
using synth::ui::KeyPicker;

MusicalScale keyOf(int root, const char* name) { return synth::makeScale(root, synth::scalePresetIndex(name)); }

struct Fixture {
    std::vector<std::optional<MusicalScale>> picks;
    std::unique_ptr<KeyPicker> picker;

    explicit Fixture(KeyPicker::Options options = {}, std::function<void(KeyPicker&)> setup = {}) {
        picker = std::make_unique<KeyPicker>(std::move(options),
                                             [this](std::optional<MusicalScale> k) { picks.push_back(k); });
        if (setup)
            setup(*picker);
    }
};

KeyPicker::Options cMajor() {
    KeyPicker::Options o;
    o.current = keyOf(0, "Major");
    return o;
}

void expectKey(const std::optional<MusicalScale>& key, int root, const char* name) {
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->rootPitchClass, root);
    EXPECT_EQ(key->name, juce::String(name));
}

} // namespace

TEST(KeyPicker, OpensOnTheCurrentKeyWithItsRowTickedAndHighlighted) {
    Fixture f(cMajor());
    EXPECT_EQ(f.picker->getRoot(), 0);
    expectKey(f.picker->getHighlighted(), 0, "Major");
    const auto keys = f.picker->visibleKeys();
    ASSERT_FALSE(keys.empty());
    EXPECT_TRUE(f.picker->isVisibleKeyTicked(0));
    EXPECT_FALSE(f.picker->isVisibleKeyTicked(1));
    EXPECT_EQ(keys.size(), 14u) << "every key scale; Chromatic is not one";
}

TEST(KeyPicker, OpensAtTheCurrentRoot) {
    KeyPicker::Options o;
    o.current = keyOf(7, "Dorian");
    Fixture f(o);
    EXPECT_EQ(f.picker->getRoot(), 7);
    expectKey(f.picker->getHighlighted(), 7, "Dorian");
}

TEST(KeyPicker, TypingARootAndScaleWordSetsTheRootAndHighlightsTheScale) {
    Fixture f(cMajor());
    f.picker->setQuery("d dor");
    EXPECT_EQ(f.picker->getRoot(), 2);
    const auto keys = f.picker->visibleKeys();
    ASSERT_FALSE(keys.empty());
    EXPECT_EQ(keys.front().name, juce::String("Dorian"));
    expectKey(f.picker->getHighlighted(), 2, "Dorian");
}

TEST(KeyPicker, AMinFiltersToTheMinorFamilyAndHighlightsTheBestScore) {
    Fixture f(cMajor());
    f.picker->setQuery("a min");
    EXPECT_EQ(f.picker->getRoot(), 9);
    const auto keys = f.picker->visibleKeys();
    // The three minors, Minor Pentatonic, and Phrygian Dominant ("Dominant" holds "min").
    ASSERT_EQ(keys.size(), 5u);
    for (const auto& k : keys)
        EXPECT_TRUE(k.name.containsIgnoreCase("min")) << k.name;

    int best = 0;
    int bestScore = synth::ui::searchScore(keys[0].name, "min");
    for (int i = 1; i < (int)keys.size(); ++i) {
        const int score = synth::ui::searchScore(keys[(size_t)i].name, "min");
        if (score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    const auto highlighted = f.picker->getHighlighted();
    ASSERT_TRUE(highlighted.has_value());
    EXPECT_EQ(highlighted->name, keys[(size_t)best].name);
    EXPECT_EQ(highlighted->rootPitchClass, 9);
}

TEST(KeyPicker, FlatAndBareNoteNamesSetTheRootAndKeepEveryScale) {
    Fixture f(cMajor());
    f.picker->setQuery("bb");
    EXPECT_EQ(f.picker->getRoot(), 10);
    f.picker->setQuery("b");
    EXPECT_EQ(f.picker->getRoot(), 11);
    EXPECT_EQ(f.picker->visibleKeys().size(), 14u);
}

TEST(KeyPicker, ScaleWordsWithoutARootKeepTheRoot) {
    Fixture f(cMajor());
    f.picker->setQuery("e");
    f.picker->setQuery("blues");
    EXPECT_EQ(f.picker->getRoot(), 4);
    const auto keys = f.picker->visibleKeys();
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0].name, juce::String("Blues"));
    EXPECT_EQ(keys[0].rootPitchClass, 4);
}

TEST(KeyPicker, ReturnPicksTheHighlightedKey) {
    Fixture f(cMajor());
    f.picker->setQuery("d dor");
    ASSERT_TRUE(f.picker->sendKey(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_EQ(f.picks.size(), 1u);
    EXPECT_TRUE(synth::sameKey(f.picks[0], keyOf(2, "Dorian")));
}

TEST(KeyPicker, UpAndDownMoveTheHighlightAndEscapeDoesNotPick) {
    Fixture f(cMajor());
    f.picker->sendKey(juce::KeyPress(juce::KeyPress::downKey));
    expectKey(f.picker->getHighlighted(), 0, "Natural Minor");
    f.picker->sendKey(juce::KeyPress(juce::KeyPress::upKey));
    expectKey(f.picker->getHighlighted(), 0, "Major");
    f.picker->sendKey(juce::KeyPress(juce::KeyPress::escapeKey));
    EXPECT_TRUE(f.picks.empty());
}

TEST(KeyPicker, RelativeFromCMajorPicksANaturalMinor) {
    Fixture f(cMajor());
    EXPECT_TRUE(f.picker->isRelativeEnabled());
    EXPECT_EQ(f.picker->getRelativeText(), juce::String("Relative: A Natural Minor"));
    f.picker->clickRelative();
    ASSERT_EQ(f.picks.size(), 1u);
    EXPECT_TRUE(synth::sameKey(f.picks[0], keyOf(9, "Natural Minor")));
}

TEST(KeyPicker, RelativeIsDisabledForADorianHighlight) {
    Fixture f(cMajor());
    f.picker->setQuery("d dor");
    EXPECT_FALSE(f.picker->isRelativeEnabled());
    f.picker->clickRelative();
    EXPECT_TRUE(f.picks.empty());
}

TEST(KeyPicker, DetectHighlightsTheKeyWithoutPickingAndSaysSo) {
    Fixture f(cMajor(),
              [](KeyPicker& p) { p.detect = [] { return std::optional<MusicalScale>(keyOf(9, "Natural Minor")); }; });
    f.picker->clickDetect();
    EXPECT_TRUE(f.picks.empty());
    EXPECT_EQ(f.picker->getRoot(), 9);
    expectKey(f.picker->getHighlighted(), 9, "Natural Minor");
    EXPECT_EQ(f.picker->getStatusText(), juce::String("Detected: A Natural Minor"));
}

TEST(KeyPicker, DetectWithNoNotesSaysThereAreNone) {
    Fixture f(cMajor(), [](KeyPicker& p) { p.detect = [] { return std::optional<MusicalScale>(); }; });
    f.picker->clickDetect();
    EXPECT_EQ(f.picker->getStatusText(), juce::String("No notes to read yet"));
    EXPECT_EQ(f.picker->getRoot(), 0);
    EXPECT_TRUE(f.picks.empty());
}

TEST(KeyPicker, NoKeyReportsNullopt) {
    Fixture f(cMajor());
    f.picker->clickNoKey();
    ASSERT_EQ(f.picks.size(), 1u);
    EXPECT_FALSE(f.picks[0].has_value());
}

TEST(KeyPicker, TheFollowRowCallsOnFollowAndPicksNothing) {
    auto o = cMajor();
    o.showFollowRow = true;
    o.followDetail = "E Natural Minor";
    int follows = 0;
    Fixture f(o, [&follows](KeyPicker& p) { p.onFollow = [&follows] { ++follows; }; });
    f.picker->setSize(KeyPicker::kWidth, f.picker->getHeight());
    f.picker->clickFollow();
    EXPECT_EQ(follows, 1);
    EXPECT_TRUE(f.picks.empty());
}

TEST(KeyPicker, AKeyboardRootClickReRootsTheRowsAndKeepsTheHighlightedScale) {
    Fixture f(cMajor());
    f.picker->sendKey(juce::KeyPress(juce::KeyPress::downKey));
    f.picker->clickKeyboardRoot(7);
    EXPECT_EQ(f.picker->getRoot(), 7);
    expectKey(f.picker->getHighlighted(), 7, "Natural Minor");
    EXPECT_FALSE(f.picker->isVisibleKeyTicked(1)) << "the tick belongs to C Major only";
}

TEST(KeyPicker, AnAuditionFiresAfterTheDelayAndEndsWhenThePickerGoes) {
    std::vector<std::optional<MusicalScale>> heard;
    {
        Fixture f(cMajor(), [&heard](KeyPicker& p) { p.onAudition = [&heard](auto k) { heard.push_back(k); }; });
        f.picker->hoverVisibleKey(2);
        EXPECT_TRUE(heard.empty()) << "not before the delay";
        synth::test::waitUntil([&] { return (heard.size()) == (1u); });
        ASSERT_EQ(heard.size(), 1u);
        expectKey(heard[0], 0, f.picker->getHighlighted()->name.toRawUTF8());
    }
    ASSERT_EQ(heard.size(), 2u);
    EXPECT_FALSE(heard[1].has_value());
}

TEST(KeyPicker, NothingIsAuditionedUntilTheUserMovesTheHighlight) {
    int calls = 0;
    {
        Fixture f(cMajor(), [&calls](KeyPicker& p) { p.onAudition = [&calls](auto) { ++calls; }; });
        juce::MessageManager::getInstance()->runDispatchLoopUntil(KeyPicker::kAuditionDelayMs + 100);
    }
    EXPECT_EQ(calls, 0);
}

TEST(KeyPicker, YourScalesFormAGroupAtTheEndAtThePickersRoot) {
    auto o = cMajor();
    o.userScales = {{"My Mode", 0x0AB5}};
    Fixture f(o);
    f.picker->setQuery("g");
    const auto keys = f.picker->visibleKeys();
    ASSERT_EQ(keys.size(), 15u);
    EXPECT_EQ(keys.back().name, juce::String("My Mode"));
    EXPECT_EQ(keys.back().rootPitchClass, 7);
    const auto groups = f.picker->visibleGroupTitles();
    ASSERT_FALSE(groups.empty());
    EXPECT_EQ(groups.back(), juce::String("Your scales"));
}

TEST(KeyPicker, OnClosedRunsOnceFromTheDestructor) {
    int closed = 0;
    {
        Fixture f(cMajor(), [&closed](KeyPicker& p) { p.onClosed = [&closed] { ++closed; }; });
        EXPECT_EQ(closed, 0);
    }
    EXPECT_EQ(closed, 1);
}

// Fit project to key: only an owner that asks for the button gets one; a press runs the callback once and leaves the
// keys alone, and a grey button does nothing.
TEST(KeyPicker, FitProjectButtonIsShownOnlyWhenAskedForAndGreyWithoutAKey) {
    Fixture plain(cMajor());
    EXPECT_FALSE(plain.picker->isFitProjectShown());

    auto options = cMajor();
    options.showFitProject = true;
    options.fitProjectEnabled = true;
    Fixture f(options);
    int fits = 0;
    f.picker->onFitProject = [&] { ++fits; };
    EXPECT_TRUE(f.picker->isFitProjectShown());
    EXPECT_TRUE(f.picker->isFitProjectEnabled());
    f.picker->clickFitProject();
    EXPECT_EQ(fits, 1);
    EXPECT_TRUE(f.picks.empty()) << "fitting is not picking a key";

    options.fitProjectEnabled = false;
    Fixture grey(options);
    grey.picker->onFitProject = [&] { ++fits; };
    EXPECT_FALSE(grey.picker->isFitProjectEnabled());
    grey.picker->clickFitProject();
    EXPECT_EQ(fits, 1);
}

TEST(KeyPicker, FitProjectButtonSitsAboveTheButtonsAndInsideThePicker) {
    auto options = cMajor();
    options.showFitProject = true;
    options.fitProjectEnabled = true;
    Fixture f(options);
    auto* fit = f.picker->findChildWithID("keyPickerFitProject");
    ASSERT_NE(fit, nullptr);
    EXPECT_EQ(fit->getTitle(), "Fit project to key");
    auto* tip = dynamic_cast<juce::TooltipClient*>(fit);
    ASSERT_NE(tip, nullptr);
    EXPECT_TRUE(tip->getTooltip().isNotEmpty());
    EXPECT_TRUE(f.picker->getLocalBounds().contains(fit->getBounds()));
    EXPECT_TRUE(fit->getWantsKeyboardFocus());
}
