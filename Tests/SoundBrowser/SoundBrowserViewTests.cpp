// The Sounds view of the left sidebar: its pure wording, keyboard and mouse navigation, search, favourites, the
// Modules / Sounds switch, the width handle, and every built-in theme rendered to a PNG strip for review.
#include "../App/MainComponent/MainComponentTestFixture.h"
#include "SoundBrowserTestKit.h"
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserText.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"

using namespace sound_browser_test;
using synth::soundbrowser::SoundKind;
using synth::ui::LibrarySidebarViews;
using synth::ui::SidebarView;
using synth::ui::SoundBrowserComponent;

namespace {

juce::KeyPress key(int code, int mods = 0) { return juce::KeyPress(code, juce::ModifierKeys(mods), 0); }

int rowIndex(const SoundBrowserComponent& view, const juce::String& text) {
    for (size_t i = 0; i < view.getRows().size(); ++i)
        if (view.getRows()[i].text == text)
            return (int)i;
    return -1;
}

struct ViewFixture {
    TempTree tree;
    synth::soundbrowser::SoundLists lists;
    std::unique_ptr<synth::soundbrowser::SoundIndex> index;
    std::unique_ptr<SoundBrowserComponent> view;
    ViewFixture() {
        tree.fill();
        index = makeIndex(tree);
        scanAndWait(*index);
        view = std::make_unique<SoundBrowserComponent>(*index, lists);
        view->setSize(300, 700);
    }
    void settle() { view->completeListAnimationForTest(); }
};

} // namespace

TEST(SoundBrowserTextTest, LengthsDetailsAndSpokenDescriptions) {
    EXPECT_EQ(synth::ui::formatSoundLength(0.8), "0.8 s");
    EXPECT_EQ(synth::ui::formatSoundLength(12.0), "12 s");
    EXPECT_EQ(synth::ui::formatSoundLength(65.0), "1:05");
    EXPECT_EQ(synth::ui::soundBarCount(120.0, 4.0), 2);
    EXPECT_EQ(synth::ui::soundBarCount(0.0, 4.0), 0);

    synth::soundbrowser::SoundEntry entry;
    entry.name = "snare";
    entry.lengthSeconds = 0.8;
    EXPECT_EQ(synth::ui::soundDetailText(entry), "0.8 s");
    EXPECT_NE(synth::ui::describeSoundForAccessibility(entry, 2, 23).indexOf("2 of 23"), -1);
    EXPECT_EQ(synth::ui::describeFolderForAccessibility("Drums", 24, 2, 5), "Drums, folder, 24 sounds, 2 of 5");
    EXPECT_EQ(synth::ui::describePathForAccessibility({"Places", "Core"}), "Path, Places, Core");
}

TEST(SoundBrowserTextTest, EveryRowEndsWithItsLengthAndTheSpokenTextReadsTheSameFields) {
    const auto dot = juce::String::fromUTF8("\xc2\xb7");
    const auto sound = [](SoundKind kind, double tempo, double seconds) {
        synth::soundbrowser::SoundEntry e;
        e.name = "dusty";
        e.kind = kind;
        e.tempo = tempo;
        e.lengthSeconds = seconds;
        return e;
    };
    struct Row {
        const char* what;
        synth::soundbrowser::SoundEntry entry;
        juce::String detail;
        juce::String spoken;
    };
    const Row rows[] = {
        {"loop with tempo", sound(SoundKind::Loop, 120.0, 4.0), "120 BPM " + dot + " 2 bars " + dot + " 4.0 s",
         "dusty, loop, 120 BPM, 2 bars, 4.0 seconds, 1 of 3"},
        {"loop of one bar", sound(SoundKind::Loop, 120.0, 2.0), "120 BPM " + dot + " 1 bar " + dot + " 2.0 s",
         "dusty, loop, 120 BPM, 1 bar, 2.0 seconds, 1 of 3"},
        {"loop under half a bar", sound(SoundKind::Loop, 120.0, 0.4), "120 BPM " + dot + " 0.4 s",
         "dusty, loop, 120 BPM, 0.4 seconds, 1 of 3"},
        {"loop with no tempo", sound(SoundKind::Loop, 0.0, 4.0), "4.0 s", "dusty, loop, 4.0 seconds, 1 of 3"},
        {"one-shot", sound(SoundKind::OneShot, 0.0, 0.8), "0.8 s", "dusty, one-shot, 0.8 seconds, 1 of 3"},
        {"instrument", sound(SoundKind::Instrument, 0.0, 0.0), "instrument", "dusty, instrument, 1 of 3"},
    };
    for (const auto& row : rows) {
        EXPECT_EQ(synth::ui::soundDetailText(row.entry), row.detail) << row.what;
        EXPECT_EQ(synth::ui::describeSoundForAccessibility(row.entry, 1, 3), row.spoken) << row.what;
    }
    auto keyed = sound(SoundKind::Loop, 120.0, 4.0);
    keyed.key = "A minor";
    EXPECT_EQ(synth::ui::describeSoundForAccessibility(keyed, 1, 3),
              "dusty, loop, 120 BPM, 2 bars, 4.0 seconds, A minor, 1 of 3");
}

TEST(SoundBrowserTextTest, ARowDetailDropsPartsFromTheLeftInsteadOfCrowdingTheName) {
    const auto dot = juce::String::fromUTF8("\xc2\xb7");
    synth::soundbrowser::SoundEntry loop;
    loop.name = "dusty";
    loop.kind = SoundKind::Loop;
    loop.tempo = 120.0;
    loop.lengthSeconds = 4.0;
    EXPECT_EQ(synth::ui::soundDetailText(loop, 2), "2 bars " + dot + " 4.0 s");
    EXPECT_EQ(synth::ui::soundDetailText(loop, 1), "4.0 s");

    const juce::Font font(juce::FontOptions(12.0f));
    const auto width = [&](const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text); };
    const float narrow = synth::ui::soundDetailMaxWidth(200); // the narrowest sidebar
    const auto fitted = synth::ui::soundDetailTextFitting(loop, font, narrow);
    EXPECT_LE(width(fitted), narrow);
    EXPECT_TRUE(fitted.endsWith("4.0 s")) << "the length always stays";
    EXPECT_GE(200 - 2 * synth::ui::kSoundRowSideMargin - width(fitted) - 8.0f, 60.0f) << "the name keeps room";
    EXPECT_EQ(synth::ui::soundDetailTextFitting(loop, font, 300.0f), synth::ui::soundDetailText(loop))
        << "a wide row shows everything";
    EXPECT_EQ(synth::ui::soundDetailTextFitting(loop, font, 1.0f), "4.0 s") << "never less than the length";
}

TEST(SoundBrowserTextTest, TheFileBrowserHasItsPlatformName) {
#if JUCE_MAC
    EXPECT_EQ(synth::ui::fileManagerName(), "Finder");
#elif JUCE_WINDOWS
    EXPECT_EQ(synth::ui::fileManagerName(), "File Explorer");
#else
    EXPECT_EQ(synth::ui::fileManagerName(), "the file manager");
#endif
}

TEST(SoundBrowserViewTest, ArrowsOpenFoldersAndGoBack) {
    ViewFixture f;
    auto& v = *f.view;
    ASSERT_GE(rowIndex(v, "Core instruments"), 0);
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::downKey)));
    ASSERT_EQ(v.getRows()[(size_t)v.getSelectedIndex()].text, "Core instruments");
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::rightKey)));
    f.settle();
    EXPECT_EQ(v.getPlaceId(), "core");
    ASSERT_GE(rowIndex(v, "Drums"), 0);
    v.selectRow(rowIndex(v, "Drums"));
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::rightKey)));
    f.settle();
    EXPECT_EQ(v.getFolder(), "Drums");
    EXPECT_GE(rowIndex(v, "snare_tight"), 0);
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::leftKey)));
    f.settle();
    EXPECT_EQ(v.getFolder(), "");
    EXPECT_TRUE(v.keyPressed(key(juce::KeyPress::leftKey)));
    f.settle();
    EXPECT_EQ(v.getPlaceId(), "");
}

TEST(SoundBrowserViewTest, ClickingAPathPartGoesBackThere) {
    ViewFixture f;
    auto& v = *f.view;
    v.showLocation("core", "Drums/Kicks", 0);
    f.settle();
    const auto parts = v.getPathParts();
    ASSERT_GE(parts.size(), 3u);
    const auto at = v.getPathPartBounds(0).getCentre();
    v.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), {}, 0, 0, 0, 0, 0, &v,
                                 &v, juce::Time::getCurrentTime(), at.toFloat(), juce::Time::getCurrentTime(), 1,
                                 false));
    f.settle();
    EXPECT_EQ(v.getPlaceId(), "");
}

TEST(SoundBrowserViewTest, SearchFiltersAcrossPlacesAndEscapeClears) {
    ViewFixture f;
    auto& v = *f.view;
    v.setQuery("snare");
    f.settle();
    ASSERT_FALSE(v.getRows().empty());
    EXPECT_EQ(v.getRows()[0].text, "snare_tight");
    EXPECT_EQ(rowIndex(v, "kick-808"), -1);
    v.setQuery({});
    f.settle();
    EXPECT_TRUE(v.getPlaceId().isEmpty());
}

TEST(SoundBrowserViewTest, FKeyFavouritesTheSelectedSoundAndSpeaksIt) {
    ViewFixture f;
    auto& v = *f.view;
    v.showLocation("core", "Drums", 0);
    f.settle();
    v.selectRow(rowIndex(v, "snare_tight"));
    EXPECT_NE(v.getAccessibilityValueText().indexOf("snare_tight"), -1);
    EXPECT_TRUE(v.keyPressed(juce::KeyPress('f', juce::ModifierKeys(), 'f')));
    ASSERT_EQ(f.lists.favourites().size(), 1);
    EXPECT_TRUE(f.lists.isFavourite(v.getRows()[(size_t)v.getSelectedIndex()].entry->path));
}

TEST(SoundBrowserSwitchTest, ArrowKeysSwitchViewsAndTheValueIsSpoken) {
    synth::ui::LibrarySidebarSwitch sw;
    sw.setSize(260, synth::ui::LibrarySidebarSwitch::kHeight);
    int chosen = -1;
    sw.onViewChosen = [&chosen](SidebarView v) { chosen = (int)v; };
    EXPECT_TRUE(sw.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(chosen, (int)SidebarView::Sounds);
    EXPECT_NE(sw.getAccessibilityValueText().indexOf("Sounds"), -1);
    EXPECT_TRUE(sw.keyPressed(key(juce::KeyPress::leftKey)));
    EXPECT_EQ(chosen, (int)SidebarView::Modules);
}

TEST(SoundBrowserSidebarTest, WidthClampsPersistsAndResets) {
    juce::PropertiesFile::Options opts;
    opts.applicationName = "sound-browser-width-test-" + juce::Uuid().toDashedString();
    opts.filenameSuffix = "settings";
    opts.folderName = juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName();
    juce::PropertiesFile settings(opts);

    ModuleLibraryComponent library;
    {
        LibrarySidebarViews sidebar(library, &settings);
        sidebar.setSize(300, 800);
        sidebar.setView(SidebarView::Sounds, false);
        EXPECT_EQ(sidebar.getSoundsWidth(), LibrarySidebarViews::kDefaultSoundsWidth);
        sidebar.setSoundsWidth(900, true);
        EXPECT_EQ(sidebar.getSoundsWidth(), LibrarySidebarViews::kMaxSoundsWidth);
        sidebar.setSoundsWidth(10, true);
        EXPECT_EQ(sidebar.getSoundsWidth(), LibrarySidebarViews::kMinSoundsWidth);
        sidebar.setSoundsWidth(333, true);
        EXPECT_EQ(sidebar.openWidth(240), 333);
        auto& handle = sidebar.getResizeHandle();
        ASSERT_TRUE(handle.onDragged != nullptr);
        handle.onDragged(20);
        EXPECT_NE(sidebar.getSoundsWidth(), 333);
        ASSERT_TRUE(handle.onResetRequested != nullptr);
        handle.onResetRequested();
        EXPECT_EQ(sidebar.getSoundsWidth(), LibrarySidebarViews::kDefaultSoundsWidth);
        sidebar.setSoundsWidth(333, true);
    }
    ModuleLibraryComponent library2;
    LibrarySidebarViews again(library2, &settings);
    EXPECT_EQ(again.getSoundsWidth(), 333);
    EXPECT_EQ(again.getView(), SidebarView::Sounds) << "the last-used view is restored";
}

// Cmd+B and Cmd+Opt+B through the real key path: closed -> view, other view -> switch, same view -> closed.
TEST_F(MainComponentTest, LibraryShortcutsFollowTheClosedModulesSoundsStateMachine) {
    MainComponent mc(std::make_unique<MockProvider>());
    mc.setSize(1400, 900);
    auto& sidebar = mc.getLibrarySidebarForTest();
    const auto press = [&mc](const char* action) {
        const auto binding = mc.getShortcutManager().getBinding(action);
        ASSERT_TRUE(binding.isValid());
        EXPECT_TRUE(mc.keyPressed(binding));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    };
    ASSERT_TRUE(mc.isLibraryConfiguredVisible());
    sidebar.setView(SidebarView::Modules, false);

    press("toggleSoundBrowser");
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(sidebar.getView(), SidebarView::Sounds);
    press("toggleLibrary");
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(sidebar.getView(), SidebarView::Modules);
    press("toggleLibrary");
    EXPECT_FALSE(mc.isLibraryConfiguredVisible());
    press("toggleSoundBrowser");
    EXPECT_TRUE(mc.isLibraryConfiguredVisible());
    EXPECT_EQ(sidebar.getView(), SidebarView::Sounds);
    press("toggleSoundBrowser");
    EXPECT_FALSE(mc.isLibraryConfiguredVisible());

    juce::Button* toggle = nullptr;
    for (auto* child : mc.getChildren())
        if (auto* btn = dynamic_cast<juce::Button*>(child))
            if (btn->getComponentID() == "toggleLibrary")
                toggle = btn;
    ASSERT_NE(toggle, nullptr);
    const auto tip = toggle->getTooltip();
    EXPECT_NE(tip.indexOf("Modules"), -1);
    EXPECT_NE(tip.indexOf("Sounds"), -1);
}

TEST_F(MainComponentTest, TheSoundBrowserShortcutIsUniqueAndSurvivesASavedKeymap) {
    juce::ApplicationProperties props;
    auto opts = synth::userSettingsOptions();
    opts.applicationName = "sound-browser-keymap-" + juce::Uuid().toDashedString();
    props.setStorageParameters(opts);
    props.getUserSettings()->setValue("shortcut_toggleLibrary",
                                      "cmd + b"); // a saved map that knows nothing of the new action
    ShortcutManager shortcuts;
    shortcuts.loadFromProperties(props);
    const auto binding = shortcuts.getBinding("toggleSoundBrowser");
    EXPECT_EQ(binding, juce::KeyPress('b', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0));
    EXPECT_NE(binding, shortcuts.getBinding("toggleLibrary"));
}

// Writes one PNG per built-in theme to SOUND_BROWSER_THEME_STRIP_DIR (when set) so the look can be reviewed.
TEST(SoundBrowserViewTest, RendersInEveryBuiltInThemeWithoutBlankOutput) {
    const juce::String dir = juce::SystemStats::getEnvironmentVariable("SOUND_BROWSER_THEME_STRIP_DIR", {});
    for (const auto& theme : synth::theme::builtInThemes()) {
        synth::theme::AppLookAndFeel laf;
        laf.applyTheme(theme);
        ViewFixture f;
        f.view->setLookAndFeel(&laf);
        f.view->showLocation("core", "Drums", 0);
        f.settle();
        f.view->selectRow(rowIndex(*f.view, "snare_tight"));
        const auto image = f.view->createComponentSnapshot(f.view->getLocalBounds());
        EXPECT_EQ(image.getPixelAt(5, 400), theme.colors.bg0) << theme.name;
        if (dir.isNotEmpty()) {
            juce::File out = juce::File(dir).getChildFile("sounds-" + theme.id + ".png");
            out.getParentDirectory().createDirectory();
            out.deleteFile();
            juce::FileOutputStream stream(out);
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
        f.view->setLookAndFeel(nullptr);
    }
}
