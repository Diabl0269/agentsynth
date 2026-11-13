// CrashReportPluginFinder: a macOS crash report in, the plugin it blames out.

#include "CrashReportFixtures.h"
#include "Plugin/Hosting/CrashReportPluginFinder.h"
#include <gtest/gtest.h>

using synth::CrashReportPluginFinder::find;
using synth::CrashReportPluginFinder::pluginFromImagePath;

TEST(CrashReportPluginFinderTest, NamesThePluginOfTheFaultingThread) {
    const auto plugin = find(synth::test::kPluginCrashReport);
    ASSERT_TRUE(plugin.has_value());
    EXPECT_EQ(plugin->name, "Example Synth");
    EXPECT_EQ(plugin->bundlePath, "/Library/Audio/Plug-Ins/VST3/Example Synth.vst3");
    EXPECT_FALSE(plugin->isAudioUnit);
}

TEST(CrashReportPluginFinderTest, AFaultingThreadWithoutAPluginFrameNamesNothing) {
    // Thread 0 (the main thread) has only our own and system frames.
    auto text =
        juce::String(synth::test::kPluginCrashReport).replace("\"faultingThread\" : 2", "\"faultingThread\" : 0");
    EXPECT_FALSE(find(text).has_value());
}

TEST(CrashReportPluginFinderTest, ReadsABodyWithNoHeaderLine) {
    auto text = juce::String(synth::test::kPluginCrashReport);
    text = text.fromFirstOccurrenceOf("\n", false, false);
    EXPECT_TRUE(find(text).has_value());
}

TEST(CrashReportPluginFinderTest, MalformedInputNamesNothing) {
    EXPECT_FALSE(find("").has_value());
    EXPECT_FALSE(find("not json").has_value());
    EXPECT_FALSE(find("{}\n{}").has_value());
    EXPECT_FALSE(find("{}\n{\"faultingThread\":9,\"threads\":[],\"usedImages\":[]}").has_value());
    // A frame pointing past the image list is skipped, not trusted.
    EXPECT_FALSE(find("{}\n{\"faultingThread\":0,\"threads\":[{\"frames\":[{\"imageIndex\":5}]}],\"usedImages\":[]}")
                     .has_value());
}

TEST(CrashReportPluginFinderTest, AnAudioUnitBundleIsRecognised) {
    const auto plugin =
        pluginFromImagePath("/Library/Audio/Plug-Ins/Components/Other Synth.component/Contents/MacOS/x");
    ASSERT_TRUE(plugin.has_value());
    EXPECT_EQ(plugin->name, "Other Synth");
    EXPECT_TRUE(plugin->isAudioUnit);
}

TEST(CrashReportPluginFinderTest, OnlyAWholeBundleComponentCounts) {
    EXPECT_FALSE(pluginFromImagePath("/usr/lib/libfoo.dylib").has_value());
    EXPECT_FALSE(pluginFromImagePath("/Applications/Agent Synth.app/Contents/MacOS/Agent Synth").has_value());
    EXPECT_FALSE(pluginFromImagePath("/tmp/notes.vst3files/x").has_value())
        << "the extension must end a path component";
    EXPECT_FALSE(pluginFromImagePath("").has_value());
}
