// DepthLevelTests.cpp -- the Depth look (Flat, Soft, Rich) chosen in Settings > Appearance > Controls: its persisted
// ids, and ThemeManager owning the choice (stamped into every theme, persisted, broadcast once, restored at startup).

#include "../../TestWait.h"
#include "../../UniqueTempDir.h"
#include "UI/Theme/BuiltInThemes.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>
#include <memory>

namespace {

using namespace synth::theme;

std::unique_ptr<juce::PropertiesFile> makeProps() {
    auto file = synth::test::uniqueTempFile("DepthLevel.settings");
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = "DepthLevelTests";
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

// An ApplicationProperties on its own throwaway file, the way a ThemeManager is handed one.
struct IsolatedProps {
    IsolatedProps() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "DepthLevelManagerTests";
        opts.folderName = "DepthLevelManagerTests";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        {
            juce::ApplicationProperties initial;
            initial.setStorageParameters(opts);
            if (auto* s = initial.getUserSettings())
                s->getFile().deleteFile();
        }
        props.setStorageParameters(opts);
    }
    ~IsolatedProps() {
        if (auto* s = props.getUserSettings())
            s->getFile().deleteFile();
    }
    juce::PropertiesFile& settings() { return *props.getUserSettings(); }
    juce::ApplicationProperties props;
};

struct CountingListener : juce::ChangeListener {
    void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    int count = 0;
};

void expectEveryThemeAt(const ThemeManager& manager, DepthLevel level) {
    ASSERT_FALSE(manager.getThemes().empty());
    for (const auto& theme : manager.getThemes())
        EXPECT_EQ(theme.treatment.depth, level) << theme.id;
    EXPECT_EQ(manager.getActiveTheme().treatment.depth, level);
}

} // namespace

TEST(DepthLevelTest, IdsRoundTripAndTheOrderIsFlatSoftRich) {
    ASSERT_EQ(kDepthLevelCount, 3);
    const char* ids[] = {"flat", "soft", "rich"};
    const char* labels[] = {"Flat", "Soft", "Rich"};
    for (int i = 0; i < kDepthLevelCount; ++i) {
        const auto level = (DepthLevel)i;
        EXPECT_STREQ(depthLevelId(level), ids[i]);
        EXPECT_STREQ(depthLevelLabel(level), labels[i]);
        EXPECT_EQ(depthLevelFromId(depthLevelId(level)), level);
    }
}

TEST(DepthLevelTest, AnUnknownOrMissingIdLoadsAsSoft) {
    EXPECT_EQ(depthLevelFromId("polished"), DepthLevel::Soft);
    EXPECT_EQ(depthLevelFromId(""), DepthLevel::Soft);
    EXPECT_EQ(depthLevelFromId("RICH"), DepthLevel::Soft) << "ids are exact, like every other persisted id";
    auto props = makeProps();
    EXPECT_EQ(loadDepthLevel(*props), DepthLevel::Soft);
    props->setValue(depthLevelKey(), "retired-level");
    EXPECT_EQ(loadDepthLevel(*props), DepthLevel::Soft);
}

TEST(DepthLevelTest, TheLevelIsWrittenUnderItsKeyAndReadBack) {
    auto props = makeProps();
    EXPECT_STREQ(depthLevelKey(), "depthLevel");
    for (const auto level : {DepthLevel::Flat, DepthLevel::Rich, DepthLevel::Soft}) {
        writeDepthLevel(*props, level);
        EXPECT_EQ(props->getValue("depthLevel"), depthLevelId(level));
        EXPECT_EQ(loadDepthLevel(*props), level);
    }
}

TEST(DepthLevelTest, ATreatmentFromAThemeFileDefaultsToSoft) {
    // Depth is a user setting, never part of a theme file: a theme as built or parsed carries the default.
    for (const auto& theme : builtInThemes())
        EXPECT_EQ(theme.treatment.depth, DepthLevel::Soft) << theme.id;
}

TEST(DepthSettingTest, TheDefaultIsSoftInEveryTheme) {
    ThemeManager manager;
    EXPECT_EQ(manager.getDepthLevel(), DepthLevel::Soft);
    expectEveryThemeAt(manager, DepthLevel::Soft);
}

TEST(DepthSettingTest, SettingTheLevelStampsEveryThemeAndBroadcastsOnce) {
    ThemeManager manager;
    CountingListener listener;
    manager.addChangeListener(&listener);

    manager.setDepthLevel(DepthLevel::Rich);
    synth::test::flushMessageQueue();
    EXPECT_EQ(manager.getDepthLevel(), DepthLevel::Rich);
    expectEveryThemeAt(manager, DepthLevel::Rich);
    EXPECT_EQ(listener.count, 1);

    manager.setDepthLevel(DepthLevel::Rich); // unchanged: nothing
    synth::test::flushMessageQueue();
    EXPECT_EQ(listener.count, 1);

    manager.setDepthLevel(DepthLevel::Flat);
    synth::test::flushMessageQueue();
    expectEveryThemeAt(manager, DepthLevel::Flat);
    EXPECT_EQ(listener.count, 2);
    manager.removeChangeListener(&listener);
}

TEST(DepthSettingTest, TheLevelIsPersistedAndRestoredByInitialise) {
    IsolatedProps rig;
    {
        ThemeManager manager;
        manager.initialise(&rig.props);
        EXPECT_FALSE(rig.settings().containsKey(depthLevelKey())) << "nothing is written until the user picks";
        manager.setDepthLevel(DepthLevel::Flat);
        EXPECT_EQ(rig.settings().getValue(depthLevelKey()), "flat");
    }
    ThemeManager restored;
    restored.initialise(&rig.props);
    EXPECT_EQ(restored.getDepthLevel(), DepthLevel::Flat);
    expectEveryThemeAt(restored, DepthLevel::Flat);
}

TEST(DepthSettingTest, AnUnknownSavedLevelStartsAsSoft) {
    IsolatedProps rig;
    rig.settings().setValue(depthLevelKey(), "gone");
    ThemeManager manager;
    manager.initialise(&rig.props);
    EXPECT_EQ(manager.getDepthLevel(), DepthLevel::Soft);
    expectEveryThemeAt(manager, DepthLevel::Soft);
}

TEST(DepthSettingTest, AThemeAddedOrSwitchedToLaterCarriesTheLevel) {
    ThemeManager manager;
    manager.setDepthLevel(DepthLevel::Rich);

    auto custom = builtInThemes().front();
    custom.id = "depth-test-custom";
    custom.treatment.depth = DepthLevel::Flat; // a stale value from elsewhere never wins
    manager.addUserTheme(custom);
    for (const auto& theme : manager.getThemes())
        EXPECT_EQ(theme.treatment.depth, DepthLevel::Rich) << theme.id;

    ASSERT_TRUE(manager.setActiveTheme("depth-test-custom"));
    EXPECT_EQ(manager.getActiveTheme().treatment.depth, DepthLevel::Rich);
    manager.setDepthLevel(DepthLevel::Soft); // and a later change reaches it too
    EXPECT_EQ(manager.getActiveTheme().treatment.depth, DepthLevel::Soft);

    manager.loadUserThemesFromFolder(); // a reload drops it and re-registers: still stamped
    expectEveryThemeAt(manager, DepthLevel::Soft);
}

TEST(DepthSettingTest, ASwitchOfThemeKeepsTheLevel) {
    ThemeManager manager;
    manager.setDepthLevel(DepthLevel::Flat);
    ASSERT_TRUE(manager.setActiveTheme("daylight"));
    EXPECT_EQ(manager.getActiveTheme().treatment.depth, DepthLevel::Flat);
}
