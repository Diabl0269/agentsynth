// SoundPackRowLicenceTests.cpp -- Settings > Sound packs: the licence is on the closed row (not only in the opened
// details), its View licence link is a Tab stop and fires from the keyboard while the row is closed, and
// "Licence not stated" shows only for a pack that states none.
#include "../../SoundPacks/SoundPackTestKit.h"
#include "../../TestWait.h"
#include "UI/Settings/SoundPacks/SoundPackRow.h"
#include <gtest/gtest.h>

using synth::ui::SoundPackRow;

namespace {

synth::soundpacks::SoundPackInstaller::Row pack(const juce::String& licence, const juce::String& url) {
    synth::soundpacks::SoundPackInstaller::Row row;
    row.id = "core";
    row.name = "Core instruments";
    row.licence = licence;
    row.licenceUrl = url;
    row.downloadBytes = 33 * 1024 * 1024;
    synth::soundpacks::IndexInstrument instrument;
    instrument.id = "flute";
    instrument.name = "Flute";
    row.instruments.push_back(instrument);
    return row;
}

bool tabStopsInclude(SoundPackRow& row, juce::Component& target) {
    juce::KeyboardFocusTraverser traverser;
    for (auto* c : traverser.getAllComponents(&row))
        if (c == &target)
            return true;
    return false;
}

} // namespace

TEST(SoundPackRowLicence, TheClosedRowShowsTheLicenceInsideItsBounds) {
    SoundPackRow row("core", {});
    row.setSize(500, SoundPackRow::kHeight);
    row.update(pack("CC0 1.0 (public domain)", "https://example.test/licence"));
    row.resized();
    ASSERT_FALSE(row.isExpanded());
    EXPECT_EQ(row.getLicenceText(), "Licence: CC0 1.0 (public domain)");
    EXPECT_TRUE(row.isLicenceLinkShown()) << "with an address the licence itself is the link";
    EXPECT_FALSE(row.getLicenceLabel().isVisible());
    EXPECT_TRUE(row.getLocalBounds().contains(row.getLicenceLink().getBounds()));
    EXPECT_EQ(row.getLicenceLink().getButtonText(), "Licence: CC0 1.0 (public domain)") << "fits whole at 500 px";
    EXPECT_TRUE(row.getDescription().contains("Licence: CC0 1.0"));
}

TEST(SoundPackRowLicence, TheLicenceDoesNotOverlapTheStateOrTheButtonsAtAnyWidth) {
    for (int width : {300, 380, 466, 700}) {
        SoundPackRow row("core", {});
        row.setSize(width, SoundPackRow::kHeight);
        auto data = pack("CC BY-NC-SA 4.0 International", "https://example.test/licence");
        data.state = synth::soundpacks::SoundPackInstaller::State::UpdateAvailable;
        data.latestVersion = "1.2.0";
        data.inIndex = true;
        data.removable = true;
        row.update(data);
        row.resized();
        const auto licence = row.getLicenceLink().getBounds();
        EXPECT_TRUE(row.getLocalBounds().contains(licence)) << width;
        EXPECT_GE(licence.getWidth(), 70) << width;
        EXPECT_FALSE(licence.intersects(row.getDownloadButton().getBounds())) << width;
        EXPECT_FALSE(licence.intersects(row.getUpdateButton().getBounds())) << width;
        EXPECT_LE(licence.getRight(), row.getUpdateButton().getX()) << width;
    }
}

TEST(SoundPackRowLicence, ViewLicenceIsATabStopAndFiresFromTheKeyboardWhileTheRowIsClosed) {
    juce::String opened;
    SoundPackRow row("core", {{}, {}, {}, [&](const juce::String& url) { opened = url; }});
    row.setSize(500, SoundPackRow::kHeight);
    row.update(pack("CC0 1.0", "https://example.test/licence"));
    row.resized();
    ASSERT_FALSE(row.isExpanded());
    auto& link = row.getLicenceLink();
    EXPECT_TRUE(link.getWantsKeyboardFocus());
    EXPECT_TRUE(tabStopsInclude(row, link)) << "closed: Tab reaches View licence";
    EXPECT_TRUE(tabStopsInclude(row, row.getDisclosure()));
    link.onClick(); // juce::Button's Return and Space handling ends in this callback
    EXPECT_EQ(opened, "https://example.test/licence");
    EXPECT_TRUE(link.getTitle().contains("Core instruments"));
    EXPECT_NE(link.getTooltip(), link.getTitle());
    EXPECT_FALSE(link.getTooltip().contains("Licence: ")) << "a tooltip never repeats the label";
}

TEST(SoundPackRowLicence, ShowSourcesIsReachedByOpeningTheRowWithTheChevronKeyThenTab) {
    SoundPackRow row("core", {});
    row.setSize(500, 300);
    auto data = pack("CC0 1.0", "https://example.test/licence");
    data.sources = juce::File::getSpecialLocation(juce::File::tempDirectory);
    row.update(data);
    row.resized();
    auto& sources = row.getCredit().getSourcesLink();
    EXPECT_FALSE(row.getCredit().isVisible()) << "closed: nothing to focus";
    EXPECT_TRUE(row.getDisclosure().pressKeyForTest(juce::KeyPress(juce::KeyPress::returnKey)));
    synth::test::waitUntil([&] { return row.isExpanded(); }); // a key press clicks on the next message
    ASSERT_TRUE(row.isExpanded());
    EXPECT_TRUE(row.getCredit().isVisible());
    EXPECT_TRUE(sources.isVisible());
    EXPECT_TRUE(tabStopsInclude(row, sources));
    EXPECT_TRUE(tabStopsInclude(row, row.getLicenceLink())) << "the licence link is still there while open";
    EXPECT_FALSE(row.getCredit().getSourcesLink().getTooltip().isEmpty());
}

TEST(SoundPackRowLicence, LicenceNotStatedShowsOnlyForAPackThatStatesNone) {
    SoundPackRow stated("core", {});
    stated.update(pack("CC BY 4.0", ""));
    EXPECT_EQ(stated.getLicenceText(), "Licence: CC BY 4.0");
    EXPECT_FALSE(stated.isLicenceLinkShown()) << "no address, no link";
    EXPECT_TRUE(stated.getLicenceLabel().isVisible()) << "shown as plain text instead";

    SoundPackRow none("core", {});
    none.update(pack("", ""));
    EXPECT_EQ(none.getLicenceText(), "Licence not stated");
}

TEST(SoundPackRowLicence, AListedPackWithoutALicenceTakesTheOneInItsInstalledManifest) {
    using namespace sound_pack_test;
    Rig rig;
    const auto zip = goodPackZip("core", "1.0.0");
    auto entry = packEntryJson("core", "1.0.0", zip);
    entry = entry.replace(R"("licence":"CC0 1.0",)", "").replace(R"("licenceUrl":"https://example.test/licence",)", "");
    const auto json = indexJsonOf({entry});
    rig.server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
    rig.server.bodies["https://example.test/core-1.0.0.zip"] = zip;
    rig.installer.refreshIndex();
    ASSERT_EQ(rig.installer.rows().size(), 1u);
    EXPECT_TRUE(rig.installer.rows().front().licence.isEmpty()) << "not installed, nothing states one yet";
    rig.installer.install("core");
    EXPECT_EQ(rig.installer.rows().front().licence, "CC0") << "the installed pack.json states CC0";
}
