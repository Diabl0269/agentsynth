// Concern: Record and Toggle Metronome default to a literal Ctrl+R / Ctrl+M on EVERY platform. Off the
// Mac Ctrl is also Cmd, so Repeat and Toggle Mod Matrix (which used to own those chords there) move;
// these tests build each platform's table through ShortcutManager::setDefaultsPlatform, so the Windows
// and Linux tables are checked from a Mac host too.
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>

namespace {

using Platform = ShortcutManager::DefaultsPlatform;

constexpr int kCtrl = juce::ModifierKeys::ctrlModifier;
const juce::KeyPress kCtrlR('r', kCtrl, 0);
const juce::KeyPress kCtrlM('m', kCtrl, 0);

// What the OS peer delivers for a physical Ctrl+R: upper-case key code, the Ctrl flag, no text.
juce::KeyPress peerKey(char letter) { return juce::KeyPress(juce::CharacterFunctions::toUpperCase(letter), kCtrl, 0); }

// Off the Mac JUCE folds Cmd onto Ctrl; a Mac-host test building the other table folds it by hand.
juce::KeyPress asPlatform(const juce::KeyPress& key, Platform platform) {
    auto flags = key.getModifiers().getRawFlags();
    if (platform == Platform::Other && (flags & juce::ModifierKeys::commandModifier) != 0)
        flags = (flags & ~juce::ModifierKeys::commandModifier) | kCtrl;
    return juce::KeyPress(key.getKeyCode(), juce::ModifierKeys(flags), key.getTextCharacter());
}

class TransportChordsTest : public ::testing::TestWithParam<Platform> {
protected:
    void SetUp() override { manager.setDefaultsPlatform(GetParam()); }
    ShortcutManager manager;
};

} // namespace

TEST_P(TransportChordsTest, RecordAndMetronomeDefaultToLiteralCtrlChords) {
    EXPECT_EQ(manager.getBinding("transportRecord"), kCtrlR);
    EXPECT_EQ(manager.getBinding("transportToggleMetronome"), kCtrlM);
}

TEST_P(TransportChordsTest, ThePeersCtrlKeysResolveToRecordAndMetronomeAndNothingElse) {
    EXPECT_EQ(manager.getActionsForKeyPress(peerKey('r')), juce::StringArray("transportRecord"));
    EXPECT_EQ(manager.getActionsForKeyPress(peerKey('m')), juce::StringArray("transportToggleMetronome"));
}

TEST_P(TransportChordsTest, EveryDefaultIsUniqueWithinItsCategoryOnThisPlatform) {
    std::map<std::pair<int, juce::String>, juce::String> seen; // (category, chord) -> action
    for (const auto& id : manager.getActionIds()) {
        const auto binding = asPlatform(manager.getBinding(id), GetParam());
        if (!binding.isValid())
            continue;
        const auto key = std::make_pair(static_cast<int>(ShortcutManager::getCategory(id)),
                                        juce::String(binding.getKeyCode()) + "/" +
                                            juce::String(binding.getModifiers().getRawFlags()));
        const auto [it, fresh] = seen.emplace(key, id);
        EXPECT_TRUE(fresh) << id << " shares a chord with " << it->second;
    }
}

TEST_P(TransportChordsTest, TheDisplacedActionsStillHaveADistinctDefault) {
    const auto matrix = manager.getBinding("toggleModMatrix");
    const auto repeat = manager.getBinding("repeatSelection");
    ASSERT_TRUE(matrix.isValid());
    ASSERT_TRUE(repeat.isValid());
    EXPECT_NE(asPlatform(matrix, GetParam()), kCtrlM);
    EXPECT_NE(asPlatform(repeat, GetParam()), kCtrlR);
    if (GetParam() == Platform::Mac) {
        EXPECT_EQ(matrix, juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0));
        EXPECT_EQ(repeat, juce::KeyPress('r', juce::ModifierKeys::commandModifier, 0));
    }
}

INSTANTIATE_TEST_SUITE_P(Platforms, TransportChordsTest, ::testing::Values(Platform::Mac, Platform::Other),
                         [](const ::testing::TestParamInfo<Platform>& info) {
                             return info.param == Platform::Mac ? "Mac" : "WindowsAndLinux";
                         });

TEST(ShortcutManagerTransportChordsTests, TheHostPicksItsOwnPlatformByDefault) {
    ShortcutManager manager;
    EXPECT_EQ(manager.getDefaultsPlatform(), ShortcutManager::hostDefaultsPlatform());
}

// ---------------------------------------------------------------------------
// Migration: an install that saved its settings before the chords existed holds both as unbound
// (and, off the Mac, holds Cmd+M / Cmd+R on the matrix and Repeat, i.e. the same Ctrl chords).
// ---------------------------------------------------------------------------
namespace {

const juce::StringArray kKeys{"shortcutMigration_transportCtrlChords",
                              "shortcut_transportRecord",
                              "shortcut_transportToggleMetronome",
                              "shortcut_toggleModMatrix",
                              "shortcut_repeatSelection",
                              "shortcut_selectNextModule"};

void seed(const std::map<juce::String, juce::KeyPress>& keys, bool alreadyMigrated) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    for (const auto& key : kKeys)
        settings->removeValue(key);
    settings->setValue("shortcutMigration_transportCtrlChords", alreadyMigrated);
    for (const auto& [id, key] : keys)
        settings->setValue("shortcut_" + id, ShortcutManager::encodeKeyPress(key));
    settings->saveIfNeeded();
}

void load(ShortcutManager& manager, Platform platform) {
    manager.setDefaultsPlatform(platform);
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);
}

class TransportChordsMigrationTest : public ::testing::TestWithParam<Platform> {
protected:
    synth::test::PersistedKeysGuard guard_{kKeys};
};

} // namespace

TEST_P(TransportChordsMigrationTest, AnOldInstallAdoptsTheChordsAndMovesTheDisplacedActions) {
    // The old defaults as a saved file holds them: Cmd (== Ctrl off the Mac) on the matrix and Repeat.
    const auto oldMatrix =
        GetParam() == Platform::Mac ? juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0) : kCtrlM;
    const auto oldRepeat =
        GetParam() == Platform::Mac ? juce::KeyPress('r', juce::ModifierKeys::commandModifier, 0) : kCtrlR;
    seed({{"transportRecord", juce::KeyPress()},
          {"transportToggleMetronome", juce::KeyPress()},
          {"toggleModMatrix", oldMatrix},
          {"repeatSelection", oldRepeat}},
         false);

    ShortcutManager manager;
    load(manager, GetParam());

    EXPECT_EQ(manager.getBinding("transportRecord"), kCtrlR);
    EXPECT_EQ(manager.getBinding("transportToggleMetronome"), kCtrlM);
    EXPECT_EQ(manager.getActionsForKeyPress(peerKey('r')), juce::StringArray("transportRecord"));
    EXPECT_EQ(manager.getActionsForKeyPress(peerKey('m')), juce::StringArray("transportToggleMetronome"));
    EXPECT_NE(asPlatform(manager.getBinding("toggleModMatrix"), GetParam()), kCtrlM);
    EXPECT_NE(asPlatform(manager.getBinding("repeatSelection"), GetParam()), kCtrlR);
    EXPECT_TRUE(manager.getBinding("toggleModMatrix").isValid());
    EXPECT_TRUE(manager.getBinding("repeatSelection").isValid());
}

TEST_P(TransportChordsMigrationTest, AUsersOwnChordsAreKept) {
    const juce::KeyPress mine('9', juce::ModifierKeys::commandModifier, 0);
    seed({{"transportRecord", mine}, {"transportToggleMetronome", juce::KeyPress()}, {"toggleModMatrix", mine}}, false);
    // (two actions on one chord is the user's own doing; the migration must not touch either.)
    ShortcutManager manager;
    load(manager, GetParam());
    EXPECT_EQ(manager.getBinding("transportRecord"), mine);
    EXPECT_EQ(manager.getBinding("toggleModMatrix"), mine);
    EXPECT_EQ(manager.getBinding("transportToggleMetronome"), kCtrlM);
}

TEST_P(TransportChordsMigrationTest, AChordSomeoneElseHoldsIsNotStolen) {
    seed({{"transportRecord", juce::KeyPress()}, {"selectNextModule", kCtrlR}}, false);
    ShortcutManager manager;
    load(manager, GetParam());
    EXPECT_FALSE(manager.getBinding("transportRecord").isValid());
    EXPECT_EQ(manager.getBinding("selectNextModule"), kCtrlR);
}

TEST_P(TransportChordsMigrationTest, ItRunsOnceSoADeliberateUnbindSticks) {
    seed({{"transportRecord", juce::KeyPress()}, {"transportToggleMetronome", juce::KeyPress()}}, true);
    ShortcutManager manager;
    load(manager, GetParam());
    EXPECT_FALSE(manager.getBinding("transportRecord").isValid());
    EXPECT_FALSE(manager.getBinding("transportToggleMetronome").isValid());
}

INSTANTIATE_TEST_SUITE_P(Platforms, TransportChordsMigrationTest, ::testing::Values(Platform::Mac, Platform::Other),
                         [](const ::testing::TestParamInfo<Platform>& info) {
                             return info.param == Platform::Mac ? "Mac" : "WindowsAndLinux";
                         });
