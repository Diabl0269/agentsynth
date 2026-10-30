// Concern: every owner of a key picker wires the hover audition and obeys the setting. A key picker owner added
// without the wiring shows up here as a row of the table that never auditions. The sequencer card is in
// SequencerKeyFieldTests.cpp: its card fixture would pull the whole card body into this file.
#include "../../TestWait.h"
#include "Music/SongKey.h"
#include "UI/Layout/FollowKeyField.h"
#include "UI/Layout/KeyPicker/KeyAuditioner.h"
#include "UI/Layout/KeyPicker/KeyPicker.h"
#include "UI/PianoRoll/ScaleAssistPanel.h"
#include "UI/Timeline/SongKeyField.h"
#include <gtest/gtest.h>
#include <juce_data_structures/juce_data_structures.h>

using synth::ui::KeyAuditioner;
using synth::ui::KeyPicker;

namespace {

struct Owner {
    std::string name;
    KeyAuditioner* auditioner = nullptr;
    std::function<void()> open;
};

// Opens each owner's picker (kept in `held`), counts the default-sound runs it starts, and hovers the second key.
struct Table {
    std::unique_ptr<juce::Component> held;
    std::vector<Owner> owners;
    int runs = 0;

    synth::ui::SongKeyField songField;
    synth::ui::FollowKeyField followField;
    synth::ui::ScaleAssistPanel panel;

    Table() {
        const auto launcher = [this](std::unique_ptr<juce::Component> picker, juce::Rectangle<int>) {
            held = std::move(picker);
        };
        songField.pickerLauncher = launcher;
        followField.pickerLauncher = launcher;
        panel.pickerLauncher = launcher;

        owners = {{"SongKeyField", &songField.getAuditioner(), [this] { songField.openPicker(); }},
                  {"FollowKeyField", &followField.getAuditioner(), [this] { followField.openPicker(); }},
                  {"ScaleAssistPanel", &panel.getAuditioner(), [this] { panel.openKeyPicker(); }}};
        for (auto& owner : owners) {
            // With no resolver, every owner takes the default sound.
            owner.auditioner->playDefaultSound = [this](const std::vector<int>&) { ++runs; };
        }
    }

    bool hoverPlays(Owner& owner) {
        const int before = runs;
        owner.open();
        auto* picker = dynamic_cast<KeyPicker*>(held.get());
        EXPECT_NE(picker, nullptr) << owner.name;
        if (picker == nullptr)
            return false;
        picker->hoverVisibleKey(1);
        synth::test::waitUntil([&] { return runs > before; }, 2000);
        const bool played = runs > before;
        held.reset();
        return played;
    }
};

} // namespace

// Every owner's picker wires the hover: the piano roll's and a sequencer card's once never did.
TEST(KeyAuditionOwners, EveryKeyPickerPlaysTheScaleOnHover) {
    Table t;
    for (auto& owner : t.owners)
        EXPECT_TRUE(t.hoverPlays(owner)) << owner.name;
}

TEST(KeyAuditionOwners, TurningTheSettingOffSilencesEveryPickerButPickingStillWorks) {
    juce::ApplicationProperties app;
    juce::PropertiesFile::Options options;
    options.applicationName = "KeyAuditionOwnersTest";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    app.setStorageParameters(options);
    auto& props = *app.getUserSettings();
    props.clear();
    props.setValue(synth::kPlayScaleOnBrowseSettingsKey, "0");

    Table t;
    for (auto& owner : t.owners)
        owner.auditioner->setSettings(&props);
    for (auto& owner : t.owners) {
        const int before = t.runs;
        owner.open();
        auto* picker = dynamic_cast<KeyPicker*>(t.held.get());
        ASSERT_NE(picker, nullptr) << owner.name;
        // The picker raises its audition after its delay whatever the setting says; what the owner does with it is
        // what the setting decides.
        int fired = 0;
        const auto original = picker->onAudition;
        picker->onAudition = [&fired, original](std::optional<synth::MusicalScale> scale) {
            ++fired;
            if (original)
                original(scale);
        };
        picker->hoverVisibleKey(1);
        synth::test::waitUntil([&] { return fired > 0; }, 2000);
        ASSERT_GT(fired, 0) << owner.name;
        EXPECT_EQ(t.runs, before) << owner.name;
        EXPECT_FALSE(owner.auditioner->isAuditioning()) << owner.name;
        t.held.reset();
    }

    int picked = 0;
    t.songField.setTransport(nullptr);
    t.followField.onPickOwn = [&picked](std::optional<synth::MusicalScale>) { ++picked; };
    t.followField.openPicker();
    auto* picker = dynamic_cast<KeyPicker*>(t.held.get());
    ASSERT_NE(picker, nullptr);
    picker->setQuery("d dor");
    picker->pickHighlighted();
    EXPECT_EQ(picked, 1) << "picking is not affected by the setting";
}
