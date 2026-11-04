// Names are shown with a capital first letter when the file's own name has no capital at all; everything that is not
// shown (search, sorting, keys, the rename text) keeps the raw name.
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"

using namespace sound_browser_test;
using synth::ui::SoundBrowserComponent;
using Kind = SoundBrowserComponent::RowKind;

TEST(SoundBrowserDisplayName, CapitalisesOnlyAnAllLowerCaseName) {
    using synth::ui::displayName;
    EXPECT_EQ(displayName("crow"), "Crow");
    EXPECT_EQ(displayName("crow caw"), "Crow caw");
    EXPECT_EQ(displayName("kick-01"), "Kick-01");
    EXPECT_EQ(displayName("iPhone"), "iPhone");
    EXPECT_EQ(displayName("808 kick"), "808 kick");
    EXPECT_EQ(displayName("Wolf"), "Wolf");
    EXPECT_EQ(displayName("snare_Tight"), "snare_Tight");
    EXPECT_EQ(displayName(""), "");
}

namespace {
struct NameFixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    NameFixture() {
        writePack(tree.packs, "core", "Core instruments");
        writeWav(tree.packs.getChildFile("core/animals/crow.wav"), 0.3);
        index = makeIndex(tree);
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
    }
    int rowOf(Kind kind) const {
        for (size_t i = 0; i < view->getRows().size(); ++i)
            if (view->getRows()[i].kind == kind)
                return (int)i;
        return -1;
    }
};
} // namespace

TEST(SoundBrowserDisplayName, RowsShowAndSpeakTheCapitalisedNameWhileTheRowKeepsTheFileName) {
    NameFixture f;
    f.view->showLocation("core", "animals");
    f.view->completeListAnimationForTest();
    const int sound = f.rowOf(Kind::Sound);
    ASSERT_GE(sound, 0);
    const auto& row = f.view->getRows()[(size_t)sound];
    EXPECT_EQ(row.text, "crow") << "the raw name stays on the row";
    EXPECT_EQ(SoundBrowserComponent::shownName(row), "Crow");
    f.view->selectRow(sound);
    EXPECT_TRUE(f.view->getAccessibilityValueText().startsWith("Crow, one-shot"));

    f.view->showLocation("core", "");
    f.view->completeListAnimationForTest();
    const int folder = f.rowOf(Kind::Folder);
    ASSERT_GE(folder, 0);
    EXPECT_EQ(f.view->getRows()[(size_t)folder].text, "animals");
    EXPECT_EQ(SoundBrowserComponent::shownName(f.view->getRows()[(size_t)folder]), "Animals");
    f.view->selectRow(folder);
    EXPECT_TRUE(f.view->getAccessibilityValueText().startsWith("Animals, folder"));
}

TEST(SoundBrowserDisplayName, SearchStillFindsTheSoundByItsFileName) {
    NameFixture f;
    f.view->setQuery("cro");
    ASSERT_EQ(f.view->getRows().size(), 1u);
    EXPECT_EQ(f.view->getRows()[0].text, "crow");
    f.view->setQuery("CRO");
    EXPECT_EQ(f.view->getRows().size(), 1u);
}
