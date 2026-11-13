// Concern: every "move this item one place" action (a card, a mixer row, a lane or modulator row) defaults to the
// same chord, Alt+Up/Down, so the keys do not drift apart per surface; and the one-shot migration
// (ShortcutManager::migrateMoveLaneAltArrows) moves an install's saved Cmd+Alt+Up/Down for the lanes there.
#include "../../TestSettingsHelpers.h"
#include "ShortcutManager/ShortcutManager.h"
#include <gtest/gtest.h>
#include <map>
#include <regex>

namespace {

const juce::ModifierKeys kAlt{juce::ModifierKeys::altModifier};
const juce::ModifierKeys kCmdAlt{juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier};

// An action that moves an item one place but keeps another chord, with the reason. Empty today: add an entry only
// with a written reason (a clash in its category that cannot move, a platform convention).
const std::map<juce::String, const char*> kOtherChord = {};

const juce::StringArray kTouchedKeys{"shortcutMigration_moveLaneAltArrows", "shortcut_timelineMoveLaneUp",
                                     "shortcut_timelineMoveLaneDown"};

void seed(const juce::KeyPress& up, const juce::KeyPress& down, bool migrated) {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    auto* settings = props.getUserSettings();
    ASSERT_NE(settings, nullptr);
    settings->setValue("shortcutMigration_moveLaneAltArrows", migrated);
    settings->setValue("shortcut_timelineMoveLaneUp", ShortcutManager::encodeKeyPress(up));
    settings->setValue("shortcut_timelineMoveLaneDown", ShortcutManager::encodeKeyPress(down));
    settings->saveIfNeeded();
}

std::pair<juce::KeyPress, juce::KeyPress> loadMoveLaneKeys() {
    ShortcutManager manager;
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::test::userSettingsTestOptions());
    manager.loadFromProperties(props);
    return {manager.getBinding("timelineMoveLaneUp"), manager.getBinding("timelineMoveLaneDown")};
}

} // namespace

// Regression test for FRO1480: the timeline's Move Lane Up/Down defaulted to Cmd+Alt+Up/Down while the canvas card
// and mixer row moves were Alt+Up/Down.
TEST(ShortcutManagerMoveItemKeys, EveryMoveRowLaneOrCardActionDefaultsToAltUpDown) {
    ShortcutManager manager;
    const std::regex moveItem(".*Move(Row|Lane|Card)(Up|Down)$");
    juce::StringArray found;
    for (const auto& id : manager.getActionIds()) {
        if (!std::regex_match(id.toStdString(), moveItem))
            continue;
        found.add(id);
        if (kOtherChord.count(id) > 0)
            continue;
        const int code = id.endsWith("Up") ? juce::KeyPress::upKey : juce::KeyPress::downKey;
        EXPECT_EQ(manager.getBinding(id), juce::KeyPress(code, kAlt, 0))
            << id << " moves an item one place: Alt+arrow, or an entry in kOtherChord with the reason";
    }
    for (const char* id : {"canvasMoveCardUp", "canvasMoveCardDown", "mixerMoveRowUp", "mixerMoveRowDown",
                           "timelineMoveLaneUp", "timelineMoveLaneDown"})
        EXPECT_TRUE(found.contains(id)) << id << ": the pattern no longer finds it";
}

TEST(ShortcutManagerMoveItemKeys, NoOtherTimelineActionHoldsAltUpOrDown) {
    ShortcutManager manager;
    EXPECT_TRUE(manager.getConflictingAction("timelineMoveLaneUp", {juce::KeyPress::upKey, kAlt, 0}).isEmpty());
    EXPECT_TRUE(manager.getConflictingAction("timelineMoveLaneDown", {juce::KeyPress::downKey, kAlt, 0}).isEmpty());
}

TEST(ShortcutManagerMoveItemKeys, ASavedCmdAltPairMovesToAltOnceAndPersists) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    seed({juce::KeyPress::upKey, kCmdAlt, 0}, {juce::KeyPress::downKey, kCmdAlt, 0}, false);
    const auto first = loadMoveLaneKeys();
    EXPECT_EQ(first.first, juce::KeyPress(juce::KeyPress::upKey, kAlt, 0));
    EXPECT_EQ(first.second, juce::KeyPress(juce::KeyPress::downKey, kAlt, 0));
    EXPECT_EQ(loadMoveLaneKeys(), first) << "the second launch reads the persisted Alt pair";
}

TEST(ShortcutManagerMoveItemKeys, AUserRebindStaysAndTheMigrationNeverFiresTwice) {
    synth::test::PersistedKeysGuard guard(kTouchedKeys);
    const juce::KeyPress shiftUp(juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0);
    seed(shiftUp, {juce::KeyPress::downKey, kCmdAlt, 0}, false);
    const auto keys = loadMoveLaneKeys();
    EXPECT_EQ(keys.first, shiftUp) << "the user's own key survives";
    EXPECT_EQ(keys.second, juce::KeyPress(juce::KeyPress::downKey, kAlt, 0));

    seed({juce::KeyPress::upKey, kCmdAlt, 0}, {juce::KeyPress::downKey, kCmdAlt, 0}, true);
    EXPECT_EQ(loadMoveLaneKeys().first, juce::KeyPress(juce::KeyPress::upKey, kCmdAlt, 0))
        << "already migrated: a deliberate Cmd+Alt is the user's choice";
}
