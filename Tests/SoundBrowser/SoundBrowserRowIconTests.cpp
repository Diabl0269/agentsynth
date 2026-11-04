// The line icon at the start of each Sounds-view row: every row kind paints one in the icon cell in every built-in
// theme, a selected row's icon takes the accent, and the name starts to the right of the icon (the folder rename
// editor included).
#include "../SoundPacks/SoundPackTestKit.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <set>

using namespace sound_browser_test;
using synth::ui::SoundBrowserComponent;
using Kind = SoundBrowserComponent::RowKind;

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(60); }

// A pack on disk (Drums/Kicks, a one-shot, a loop, Piano/upright.sfz), a pack on offer ("animals"), a user folder
// "other" and the folder book that gives the Add folder row.
struct IconRig : sound_pack_test::Rig {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<synth::soundbrowser::SoundFolderBook> book;
    std::unique_ptr<SoundBrowserComponent> view;
    juce::File other = tree.dir.getChildFile("other");

    IconRig() {
        tree.fill();
        writeWav(tree.packs.getChildFile("core/Drums/dusty_120_Am_loop.wav"), 2.0);
        writeWav(other.getChildFile("kick_one.wav"), 0.2);
        index = std::make_unique<synth::soundbrowser::SoundIndex>(
            juce::File(), [this] { return std::vector<juce::File>{tree.packs}; });
        book = std::make_unique<synth::soundbrowser::SoundFolderBook>(*index);
        book->goLive();
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
        view->setFolderBook(book.get());
        view->setPackInstaller(&installer);
        publish("animals", "1.0.0");
        scanAndWait(*index);
        view->addFolders({other});
        EXPECT_TRUE(index->waitUntilIdleForTests(20000));
        pump();
        view->showLocation({}, {});
        view->completeListAnimationForTest();
    }
    void go(const juce::String& place, const juce::String& folder) {
        view->showLocation(place, folder);
        view->completeListAnimationForTest();
    }
};

juce::Image paint(SoundBrowserComponent& view) {
    juce::Image image(juce::Image::ARGB, view.getWidth(), view.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    view.paintEntireComponent(g, false);
    return image;
}

int distance(juce::Colour a, juce::Colour b) {
    return std::abs(a.getRed() - b.getRed()) + std::abs(a.getGreen() - b.getGreen()) +
           std::abs(a.getBlue() - b.getBlue());
}

// Where the icon is drawn on the row: the 14 px cell at the side margin.
juce::Rectangle<int> iconBox(juce::Rectangle<int> row) {
    return synth::ui::rowIconCell(row).getSmallestIntegerContainer();
}

juce::Colour backgroundOf(const juce::Image& image, juce::Rectangle<int> row) {
    return image.getPixelAt(row.getX() + 2, row.getCentreY());
}

int inkPixels(const juce::Image& image, juce::Rectangle<int> box, juce::Colour background) {
    int n = 0;
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            n += distance(image.getPixelAt(x, y), background) > 40 ? 1 : 0;
    return n;
}

// The pixel in `box` furthest from the background: the middle of a stroke, where the icon's own colour shows.
juce::Colour strongestPixel(const juce::Image& image, juce::Rectangle<int> box, juce::Colour background) {
    juce::Colour best = background;
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            if (distance(image.getPixelAt(x, y), background) > distance(best, background))
                best = image.getPixelAt(x, y);
    return best;
}

juce::String iconPixels(const juce::Image& image, juce::Rectangle<int> box) {
    juce::String s;
    for (int y = box.getY(); y < box.getBottom(); ++y)
        for (int x = box.getX(); x < box.getRight(); ++x)
            s << juce::String::toHexString((int)image.getPixelAt(x, y).getARGB()) << ",";
    return s;
}

} // namespace

TEST(SoundBrowserRowIcons, EveryRowKindPaintsItsIconBesideTheNameInEveryTheme) {
    IconRig rig;
    auto& view = *rig.view;
    synth::theme::AppLookAndFeel laf;
    view.setLookAndFeel(&laf);
    for (const auto& theme : synth::theme::builtInThemes()) {
        laf.applyTheme(theme);
        std::set<juce::String> seen;
        int icons = 0;
        const std::pair<juce::String, juce::String> places[] = {{{}, {}}, {"core", "Drums"}, {"core", "Piano"}};
        for (const auto& [place, folder] : places) {
            rig.go(place, folder);
            const auto image = paint(view);
            for (int i = 0; i < (int)view.getRows().size(); ++i) {
                const auto& row = view.getRows()[(size_t)i];
                if (row.kind == Kind::Hint)
                    continue;
                const auto bounds = view.getRowBounds(i);
                const auto bg = backgroundOf(image, bounds);
                const auto label = theme.id + " " + row.text;
                EXPECT_GE(inkPixels(image, iconBox(bounds), bg), 6) << label << ": no icon in its cell";
                // The 8 px gap after the icon is empty, and the name's ink starts after it.
                const int gapLeft = bounds.getX() + synth::ui::kSoundRowSideMargin + synth::ui::kSoundRowIconSize + 1;
                // The Get more heading has a 9 px chevron instead of the 14 px icon, so its gap differs.
                if (row.kind != Kind::PackHeading)
                    EXPECT_EQ(inkPixels(image,
                                        {gapLeft, bounds.getY(), synth::ui::kSoundRowIconGap - 2, bounds.getHeight()},
                                        bg),
                              0)
                        << label << ": something paints in the gap between icon and name";
                EXPECT_GE(
                    inkPixels(image,
                              {bounds.getX() + synth::ui::kSoundRowTextLeft, bounds.getY(), 40, bounds.getHeight()},
                              bg),
                    10)
                    << label << ": the name does not start right of the icon";
                seen.insert(iconPixels(image, iconBox(bounds)));
                ++icons;
            }
        }
        // Places: favourites, recent, pack, offered pack, user folder, add folder; then folder, one-shot, loop,
        // instrument: ten rows, and the artwork differs wherever the icon or its colour differs.
        EXPECT_GE(icons, 10) << theme.id;
        EXPECT_GE((int)seen.size(), 8) << theme.id << ": the row kinds share artwork they should not";
    }
    view.setLookAndFeel(nullptr);
}

TEST(SoundBrowserRowIcons, ASelectedRowsIconUsesTheAccentAndTheOthersTheMutedText) {
    IconRig rig;
    auto& view = *rig.view;
    synth::theme::AppLookAndFeel laf;
    view.setLookAndFeel(&laf);
    for (const auto& theme : synth::theme::builtInThemes()) {
        laf.applyTheme(theme);
        rig.go("core", "Drums");
        int sound = -1;
        for (int i = 0; i < (int)view.getRows().size(); ++i)
            if (view.getRows()[(size_t)i].kind == Kind::Sound) {
                sound = i;
                break;
            }
        ASSERT_GE(sound, 0);
        const auto& colors = synth::theme::themeOf(view).colors;
        view.selectRow(sound);
        const auto image = paint(view);
        for (int i = 0; i < (int)view.getRows().size(); ++i) {
            const auto bounds = view.getRowBounds(i);
            const auto ink = strongestPixel(image, iconBox(bounds), backgroundOf(image, bounds));
            const bool accentCloser = distance(ink, colors.accent) < distance(ink, colors.textMuted);
            EXPECT_EQ(accentCloser, i == sound) << theme.id << " row " << i;
        }
    }
    view.setLookAndFeel(nullptr);
}

TEST(SoundBrowserRowIcons, TheRenameEditorStartsWhereTheNameDoes) {
    IconRig rig;
    auto& view = *rig.view;
    int row = -1;
    for (int i = 0; i < (int)view.getRows().size(); ++i)
        if (view.getRows()[(size_t)i].userFolder)
            row = i;
    ASSERT_GE(row, 0);
    view.selectRow(row);
    ASSERT_TRUE(view.applyFolderAction(row, SoundBrowserComponent::FolderAction::Rename));
    EXPECT_EQ(view.getRenameLabel().getX(), synth::ui::kSoundRowTextLeft - 4);
}
