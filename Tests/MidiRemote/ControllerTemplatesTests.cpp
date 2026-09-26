// synth::midi template helpers (Source/MidiRemote/ControllerTemplates.cpp): the generic
// controller templates shipped in BinaryData, and merging one into a ControllerProfile.
// Headless -- no ControllerProfileStore, no file I/O (templates come from BinaryData).

#include "MidiRemote/ControllerTemplates.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

using namespace synth;
using namespace synth::midi;

namespace {

const std::vector<std::pair<juce::String, size_t>> kExpected = {
    {"template-8-knobs", 8u},
    {"template-8-faders-8-buttons", 16u},
    {"template-transport-strip", 4u},
    {"template-keyboard-8-knobs", 11u},
    // Vendor templates (FRO143): not in ControllerTemplates.cpp's kOrder table, so they sort after
    // the generic ones, alphabetically by id -- see ControllerTemplatesVendorTests.cpp for their
    // vendor/source coverage.
    {"template-arturia-beatstep", 32u},
    {"template-arturia-minilab-3", 20u},
    {"template-korg-nanokontrol2", 51u},
    {"template-novation-launch-control-xl-3", 48u},
};

ControllerProfile loadOrFail(const juce::String& id) {
    ControllerProfile p;
    EXPECT_TRUE(loadControllerTemplate(id, p)) << id.toStdString();
    return p;
}

int maxRow(const ControllerProfile& p) {
    int row = 0;
    for (const auto& c : p.controls)
        row = std::max(row, c.layout.row);
    return row;
}

const Control* findByCc(const ControllerProfile& p, int cc) {
    for (const auto& c : p.controls)
        if (c.message.type == MessageType::cc && c.message.number == cc)
            return &c;
    return nullptr;
}

} // namespace

TEST(ControllerTemplatesTest, ListReturnsEveryTemplateInDocumentedOrder) {
    const auto list = listControllerTemplates();
    ASSERT_EQ(list.size(), kExpected.size());
    for (size_t i = 0; i < kExpected.size(); ++i) {
        EXPECT_EQ(list[i].id, kExpected[i].first);
        EXPECT_FALSE(list[i].name.isEmpty());
    }
    EXPECT_EQ(list[0].name, "8 knobs");
    EXPECT_EQ(list[1].name, "8 faders + 8 buttons");
    EXPECT_EQ(list[2].name, "Transport strip");
    EXPECT_EQ(list[3].name, "Keyboard with 8 knobs");
    EXPECT_EQ(list[4].name, "BeatStep");
    EXPECT_EQ(list[5].name, "MiniLab 3");
    EXPECT_EQ(list[6].name, "nanoKONTROL2");
    EXPECT_EQ(list[7].name, "Launch Control XL 3");
    // The 4 generic templates carry no vendor/source; the hardware templates do.
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(list[i].vendor.isEmpty()) << list[i].id.toStdString();
        EXPECT_TRUE(list[i].source.isEmpty()) << list[i].id.toStdString();
    }
    for (size_t i = 4; i < list.size(); ++i) {
        EXPECT_FALSE(list[i].vendor.isEmpty()) << list[i].id.toStdString();
        EXPECT_FALSE(list[i].source.isEmpty()) << list[i].id.toStdString();
    }
}

TEST(ControllerTemplatesTest, EveryTemplateLoadsWithTheDocumentedControlCountAndNoDuplicateKeys) {
    for (const auto& [id, count] : kExpected) {
        ControllerProfile p;
        ASSERT_TRUE(loadControllerTemplate(id, p)) << id.toStdString();
        EXPECT_EQ(p.id, id);
        EXPECT_EQ(p.controls.size(), count) << id.toStdString();
        EXPECT_TRUE(p.actions.empty());

        std::set<juce::String> ids;
        for (size_t i = 0; i < p.controls.size(); ++i) {
            EXPECT_TRUE(ids.insert(p.controls[i].id).second) << "duplicate control id in " << id.toStdString();
            for (size_t j = i + 1; j < p.controls.size(); ++j)
                EXPECT_NE(p.controls[i].message, p.controls[j].message) << id.toStdString();
        }
    }
}

TEST(ControllerTemplatesTest, UnknownIdFailsAndLeavesOutputUntouched) {
    ControllerProfile p;
    p.name = "sentinel";
    EXPECT_FALSE(loadControllerTemplate("template-does-not-exist", p));
    EXPECT_FALSE(loadControllerTemplate("", p));
    EXPECT_EQ(p.name, "sentinel");
}

TEST(ControllerTemplatesTest, KeyboardTemplateHasWheelsSustainAndKnobRow) {
    const auto tmpl = loadOrFail("template-keyboard-8-knobs");
    int wheels = 0;
    for (const auto& c : tmpl.controls)
        if (c.kind == ControlKind::wheel)
            ++wheels;
    EXPECT_EQ(wheels, 2);
    const Control* sustain = findByCc(tmpl, 64);
    ASSERT_NE(sustain, nullptr);
    EXPECT_EQ(sustain->kind, ControlKind::button);
    EXPECT_EQ(maxRow(tmpl), 1);
}

TEST(ControllerTemplatesTest, ApplyToEmptyProfileKeepsLayoutAndAssignsFreshUniqueIds) {
    const auto tmpl = loadOrFail("template-8-faders-8-buttons");
    ControllerProfile profile;
    profile.id = "p1";
    profile.name = "My controller";
    profile.input.identifier = "dev";

    const auto result = applyControllerTemplate(profile, tmpl);

    EXPECT_EQ(result.added, 16);
    EXPECT_EQ(result.skippedDuplicates, 0);
    ASSERT_EQ(profile.controls.size(), 16u);
    EXPECT_EQ(profile.id, "p1");
    EXPECT_EQ(profile.name, "My controller");
    EXPECT_EQ(profile.input.identifier, "dev");
    EXPECT_TRUE(profile.actions.empty());

    std::set<juce::String> ids;
    for (size_t i = 0; i < profile.controls.size(); ++i) {
        const auto& c = profile.controls[i];
        EXPECT_TRUE(ids.insert(c.id).second);
        EXPECT_NE(c.id, tmpl.controls[i].id);
        EXPECT_FALSE(c.id.isEmpty());
        EXPECT_EQ(c.layout.col, tmpl.controls[i].layout.col);
        EXPECT_EQ(c.layout.row, tmpl.controls[i].layout.row);
        EXPECT_EQ(c.message, tmpl.controls[i].message);
    }
}

TEST(ControllerTemplatesTest, ApplyToNonEmptyProfileKeepsExistingControlAndStacksNewOnesBelow) {
    const auto tmpl = loadOrFail("template-8-knobs");

    ControllerProfile profile;
    Control existing;
    existing.id = "existing-1";
    existing.name = "My Cutoff";
    existing.kind = ControlKind::fader;
    existing.message.type = MessageType::cc;
    existing.message.channel = 0;
    existing.message.number = 21;
    existing.layout.col = 3;
    existing.layout.row = 2;
    profile.controls.push_back(existing);

    const auto result = applyControllerTemplate(profile, tmpl);

    EXPECT_EQ(result.skippedDuplicates, 1);
    EXPECT_EQ(result.added, 7);
    ASSERT_EQ(profile.controls.size(), 8u);

    // The pre-existing CC 21 control is untouched.
    EXPECT_EQ(profile.controls[0].id, "existing-1");
    EXPECT_EQ(profile.controls[0].name, "My Cutoff");
    EXPECT_EQ(profile.controls[0].kind, ControlKind::fader);
    EXPECT_EQ(profile.controls[0].layout.row, 2);

    // New controls sit below the existing rows (max existing row 2 -> template row 0 lands on 3).
    for (size_t i = 1; i < profile.controls.size(); ++i)
        EXPECT_EQ(profile.controls[i].layout.row, 3);

    // No two controls share a message key after the merge.
    for (size_t i = 0; i < profile.controls.size(); ++i)
        for (size_t j = i + 1; j < profile.controls.size(); ++j)
            EXPECT_NE(profile.controls[i].message, profile.controls[j].message);
}

TEST(ControllerTemplatesTest, ApplyingTheSameTemplateTwiceAddsNothingTheSecondTime) {
    const auto tmpl = loadOrFail("template-transport-strip");
    ControllerProfile profile;

    const auto first = applyControllerTemplate(profile, tmpl);
    EXPECT_EQ(first.added, 4);
    EXPECT_EQ(first.skippedDuplicates, 0);

    const auto second = applyControllerTemplate(profile, tmpl);
    EXPECT_EQ(second.added, 0);
    EXPECT_EQ(second.skippedDuplicates, 4);
    EXPECT_EQ(profile.controls.size(), 4u);
}
