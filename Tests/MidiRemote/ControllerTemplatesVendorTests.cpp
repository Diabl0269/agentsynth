// synth::midi vendor controller templates (Source/MidiRemote/ControllerTemplates.cpp), FRO143: the
// real-hardware templates (Korg nanoKONTROL2, Arturia MiniLab 3) shipped alongside the generic
// ones, their vendor/source metadata, the Templates-menu vendor grouping, and layout-cell safety
// (no two controls sharing a physical position). Headless -- no ControllerProfileStore, no file
// I/O (templates come from BinaryData).

#include "MidiRemote/ControllerTemplates.h"

#include <gtest/gtest.h>

#include <set>
#include <utility>

using namespace synth;
using namespace synth::midi;

namespace {

const std::vector<juce::String> kVendorTemplateIds = {
    "template-korg-nanokontrol2",
    "template-arturia-minilab-3",
};

} // namespace

TEST(ControllerTemplatesVendorTest, EveryVendorTemplateHasANonEmptySourceCitation) {
    for (const auto& info : listControllerTemplates()) {
        if (info.vendor.isEmpty())
            continue;
        EXPECT_FALSE(info.source.isEmpty()) << info.id.toStdString();
        // A citation names at least the document it came from -- catches an accidental
        // placeholder/empty-ish string slipping through.
        EXPECT_GT(info.source.length(), 20) << info.id.toStdString();
    }
}

TEST(ControllerTemplatesVendorTest, EveryVendorTemplateLoadsWithNoOverlappingLayoutCells) {
    for (const auto& id : kVendorTemplateIds) {
        ControllerProfile p;
        ASSERT_TRUE(loadControllerTemplate(id, p)) << id.toStdString();
        std::set<std::pair<int, int>> cells;
        for (const auto& c : p.controls) {
            const auto cell = std::make_pair(c.layout.col, c.layout.row);
            EXPECT_TRUE(cells.insert(cell).second)
                << id.toStdString() << " control " << c.id.toStdString() << " overlaps another at col=" << cell.first
                << " row=" << cell.second;
        }
    }
}

TEST(ControllerTemplatesVendorTest, KorgNanoKontrol2HasTheDocumentedSurface) {
    ControllerProfile p;
    ASSERT_TRUE(loadControllerTemplate("template-korg-nanokontrol2", p));
    EXPECT_EQ(p.controls.size(), 51u);
    int knobs = 0, faders = 0, buttons = 0;
    for (const auto& c : p.controls) {
        switch (c.kind) {
        case ControlKind::knob:
            ++knobs;
            break;
        case ControlKind::fader:
            ++faders;
            break;
        case ControlKind::button:
            ++buttons;
            break;
        default:
            ADD_FAILURE() << "unexpected kind for " << c.id.toStdString();
        }
        // Every control's channel is documented as "any" (see the template's own "source"): the
        // manual's Native-KORG-mode table gives no factory Global/Group MIDI Channel value.
        EXPECT_EQ(c.message.channel, 0);
        EXPECT_EQ(c.message.type, MessageType::cc);
    }
    EXPECT_EQ(knobs, 8);
    EXPECT_EQ(faders, 8); // sliders map onto Control::kind == fader (no separate "slider" kind)
    EXPECT_EQ(buttons, 51 - 16);
}

TEST(ControllerTemplatesVendorTest, ArturiaMiniLab3HasTheDocumentedSurface) {
    ControllerProfile p;
    ASSERT_TRUE(loadControllerTemplate("template-arturia-minilab-3", p));
    EXPECT_EQ(p.controls.size(), 20u);
    int encoders = 0, faders = 0, pads = 0;
    for (const auto& c : p.controls) {
        switch (c.kind) {
        case ControlKind::encoder:
            ++encoders;
            EXPECT_EQ(c.message.type, MessageType::cc);
            EXPECT_EQ(c.message.channel, 0); // not documented for the fixed ARTURIA-mode mapping
            break;
        case ControlKind::fader:
            ++faders;
            EXPECT_EQ(c.message.type, MessageType::cc);
            EXPECT_EQ(c.message.channel, 0);
            break;
        case ControlKind::pad:
            ++pads;
            EXPECT_EQ(c.message.type, MessageType::note);
            EXPECT_EQ(c.message.channel, 10); // documented: pads always transmit on channel 10
            break;
        default:
            ADD_FAILURE() << "unexpected kind for " << c.id.toStdString();
        }
    }
    EXPECT_EQ(encoders, 8);
    EXPECT_EQ(faders, 4);
    EXPECT_EQ(pads, 8);
}

TEST(ControllerTemplatesVendorTest, GroupingPutsGenericFirstThenVendorsAlphabeticallyPreservingOrder) {
    const auto groups = groupControllerTemplatesByVendor(listControllerTemplates());
    ASSERT_GE(groups.size(), 3u);
    EXPECT_TRUE(groups[0].vendor.isEmpty());
    EXPECT_EQ(groups[0].templates.size(), 4u);
    // Arturia < Korg alphabetically.
    EXPECT_EQ(groups[1].vendor, "Arturia");
    ASSERT_EQ(groups[1].templates.size(), 1u);
    EXPECT_EQ(groups[1].templates[0].id, "template-arturia-minilab-3");
    EXPECT_EQ(groups[2].vendor, "Korg");
    ASSERT_EQ(groups[2].templates.size(), 1u);
    EXPECT_EQ(groups[2].templates[0].id, "template-korg-nanokontrol2");
}

TEST(ControllerTemplatesVendorTest, GroupingWithNoVendorTemplatesIsJustOneGenericGroup) {
    const std::vector<TemplateInfo> onlyGeneric = {
        {"template-a", "A", {}, {}},
        {"template-b", "B", {}, {}},
    };
    const auto groups = groupControllerTemplatesByVendor(onlyGeneric);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_TRUE(groups[0].vendor.isEmpty());
    EXPECT_EQ(groups[0].templates.size(), 2u);
}

TEST(ControllerTemplatesVendorTest, GroupingWithNoGenericTemplatesOmitsTheGenericGroup) {
    const std::vector<TemplateInfo> onlyVendor = {
        {"template-x", "X", "Zorp", "https://example.invalid/manual"},
    };
    const auto groups = groupControllerTemplatesByVendor(onlyVendor);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].vendor, "Zorp");
}

TEST(ControllerTemplatesVendorTest, AllTemplateIdsAreUniqueAcrossTheWholeLibrary) {
    std::set<juce::String> ids;
    for (const auto& info : listControllerTemplates())
        EXPECT_TRUE(ids.insert(info.id).second) << info.id.toStdString();
}
