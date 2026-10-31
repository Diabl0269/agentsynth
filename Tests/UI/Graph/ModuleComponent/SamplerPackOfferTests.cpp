// SamplerPackOfferTests.cpp -- the offer under the Sampler's instrument list: made only while no pack is installed,
// downloads in place with progress, fills the list when done and goes away. The installer is the app's shared one,
// faked at the network.
#include "../../../SoundPacks/SoundPackTestKit.h"
#include "../CardBody/CardCanvas.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/DrumKitChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerChrome.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerPackOffer.h"

using namespace cardbody_test;
using namespace sound_pack_test;
using synth::ui::ModMatrixPicker;
using synth::ui::SamplerChrome;
using synth::ui::SamplerPackOffer;

namespace {

// The shared installer pointed at a scratch folder and a fake server, kept alive for the whole test.
struct SharedRig {
    juce::SharedResourcePointer<SoundPackInstaller> shared;
    TempDir dir;
    FakeServer server;
    SharedRig() {
        synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{dir.path()});
        shared->setInstallRootForTesting(dir.path());
        shared->setIndexUrlForTesting(kIndexUrl);
        shared->setFetcherForTesting(server.fetcher());
        shared->setRunInlineForTesting(true);
        const auto zip = goodPackZip("core", "1.0.0");
        const auto json = indexJson("core", "1.0.0", zip);
        server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
        server.bodies["https://example.test/core-1.0.0.zip"] = zip;
    }
    ~SharedRig() { synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt); }
};

struct Scene {
    CardCanvas canvas;
    NodeID id;
    Scene() {
        id = canvas.add(std::make_unique<SamplerModule>(), 0, 0);
        canvas.editor.updateComponents();
    }
    SamplerChrome& chrome() { return *dynamic_cast<SamplerChrome*>(canvas.card(id)->getInstrumentChrome()); }
    std::unique_ptr<ModMatrixPicker> openPicker() {
        std::unique_ptr<ModMatrixPicker> list;
        SamplerChrome::pickerHookForTest() = [&list](std::unique_ptr<ModMatrixPicker> p) { list = std::move(p); };
        chrome().getPicker().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        SamplerChrome::pickerHookForTest() = nullptr;
        return list;
    }
};

SamplerPackOffer* offerOf(ModMatrixPicker& picker) { return dynamic_cast<SamplerPackOffer*>(picker.getFooter()); }

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

} // namespace

TEST(SamplerPackOffer, IsMadeWhenNoPackIsInstalledAndOffersTheCorePackWithItsSize) {
    SharedRig rig;
    Scene scene;
    auto list = scene.openPicker();
    ASSERT_NE(list, nullptr);
    auto* offer = offerOf(*list);
    ASSERT_NE(offer, nullptr);
    EXPECT_EQ(offer->getMessageText(), "Get the core instruments (1 MB)") << "size comes from the hosted list";
    EXPECT_TRUE(offer->getDownloadButton().isEnabled());
    EXPECT_TRUE(offer->isDownloadShown());
    EXPECT_FALSE(offer->isBusyShown());
}

TEST(SamplerPackOffer, IsNotMadeWhenAPackIsAlreadyInstalled) {
    SharedRig rig;
    const auto pack = rig.dir.path().getChildFile("keys");
    pack.createDirectory();
    pack.getChildFile("a.sfz").replaceWithText("<region> sample=x.wav");
    pack.getChildFile("pack.json")
        .replaceWithText(R"({"id":"keys","instruments":[{"id":"p","name":"P","sfz":"a.sfz"}]})");
    Scene scene;
    auto list = scene.openPicker();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->getFooter(), nullptr);
}

TEST(SamplerPackOffer, DownloadsInPlaceThenFillsTheListAndGoesAway) {
    SharedRig rig;
    Scene scene;
    auto list = scene.openPicker();
    ASSERT_NE(list, nullptr);
    auto* offer = offerOf(*list);
    ASSERT_NE(offer, nullptr);
    const int heightWithOffer = list->getHeightForTest();
    EXPECT_EQ(list->getVisibleItemTextsForTest().front(), "No instruments installed yet");

    juce::String during;
    bool cancelShown = false;
    rig.server.onChunk = [&] {
        during = offer->getMessageText();
        cancelShown = offer->isBusyShown();
    };
    offer->getDownloadButton().onClick();

    EXPECT_TRUE(during.startsWith("Downloading the core instruments ")) << during;
    EXPECT_TRUE(cancelShown);
    const auto items = list->getVisibleItemTextsForTest();
    ASSERT_FALSE(items.empty());
    EXPECT_EQ(items.front(), "Grand") << "the list filled without reopening";
    EXPECT_FALSE(offer->isOfferShown());

    settle();
    EXPECT_EQ(list->getFooter(), nullptr);
    EXPECT_LT(list->getHeightForTest(), heightWithOffer);
}

TEST(SamplerPackOffer, ShowsAFailureAndTriesAgain) {
    SharedRig rig;
    rig.server.bodies["https://example.test/core-1.0.0.zip"] = juce::MemoryBlock("not a zip", 9);
    Scene scene;
    auto list = scene.openPicker();
    auto* offer = offerOf(*list);
    ASSERT_NE(offer, nullptr);
    offer->getDownloadButton().onClick();
    EXPECT_TRUE(offer->getMessageText().contains("checksum")) << offer->getMessageText();
    EXPECT_EQ(offer->getDownloadButton().getButtonText(), "Try again");
    EXPECT_TRUE(offer->getDownloadButton().isEnabled());
    EXPECT_NE(offerOf(*list), nullptr) << "the offer stays when nothing was installed";
}

TEST(SamplerPackOffer, OfflineSaysSoWithRetry) {
    SharedRig rig;
    rig.server.offline = true;
    Scene scene;
    auto list = scene.openPicker();
    auto* offer = offerOf(*list);
    ASSERT_NE(offer, nullptr);
    EXPECT_EQ(offer->getMessageText(), "Couldn't reach the pack list.");
    EXPECT_EQ(offer->getDownloadButton().getButtonText(), "Retry");
    rig.server.offline = false;
    offer->getDownloadButton().onClick();
    EXPECT_EQ(offer->getDownloadButton().getButtonText(), "Download");
}

TEST(SamplerPackOffer, ControlsHaveKeyboardStopsNamesAndTooltips) {
    SharedRig rig;
    Scene scene;
    auto list = scene.openPicker();
    auto* offer = offerOf(*list);
    ASSERT_NE(offer, nullptr);
    for (auto* button : {&offer->getDownloadButton(), &offer->getCancelButton()}) {

        EXPECT_TRUE(button->getTitle().contains("core instruments"));
        EXPECT_TRUE(button->getTooltip().contains("core instruments"));
    }
    EXPECT_TRUE(offer->getDownloadButton().getWantsKeyboardFocus());
    offer->getCancelButton().setEnabled(true); // enabled only while a download runs
    EXPECT_TRUE(offer->getCancelButton().getWantsKeyboardFocus());
    EXPECT_TRUE(offer->getProgress().getTitle().contains("percent"));
}

// The offer is part of the shared picker row, so the Drum Kit's kit list makes it too.
TEST(SamplerPackOffer, IsAlsoMadeUnderTheDrumKitsList) {
    SharedRig rig;
    CardCanvas canvas;
    const auto id = canvas.add(std::make_unique<DrumKitModule>(), 0, 0);
    canvas.editor.updateComponents();
    auto* chrome = dynamic_cast<synth::ui::DrumKitChrome*>(canvas.card(id)->getInstrumentChrome());
    ASSERT_NE(chrome, nullptr);
    std::unique_ptr<ModMatrixPicker> list;
    synth::ui::InstrumentPickerRow::pickerHookForTest() = [&list](std::unique_ptr<ModMatrixPicker> p) {
        list = std::move(p);
    };
    chrome->getRow().picker().keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    synth::ui::InstrumentPickerRow::pickerHookForTest() = nullptr;
    ASSERT_NE(list, nullptr);
    EXPECT_NE(offerOf(*list), nullptr);
}
