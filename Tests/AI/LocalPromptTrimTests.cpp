#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/ProjectGenerate/LocalPromptTrim.h"
#include "AI/generated/ProjectGenerate.g.h"
#include <gtest/gtest.h>

using namespace synth;
using namespace synth::projectgenerate;

namespace {
juce::String vendoredPrompt() {
    juce::String prompt;
    for (int i = 0; i < generated::kProjectGeneratePromptPartCount; ++i)
        prompt << generated::kProjectGeneratePromptParts[i];
    return prompt;
}
} // namespace

TEST(LocalPromptTrimTest, OmitsOnlyTheNamedSectionsAndKeepsTheRestByteForByte) {
    const juce::String text = "intro\n### A one\nbody a\n### B two\nbody b\n### C three\nbody c";
    EXPECT_EQ(omitSections(text, {"B"}), "intro\n### A one\nbody a\n### C three\nbody c");
    EXPECT_EQ(omitSections(text, {"A", "C"}), "intro\n### B two\nbody b\n");
    EXPECT_EQ(omitSections(text, {}), text);
}

TEST(LocalPromptTrimTest, ReportsAHeadingThatMatchedNothing) {
    juce::StringArray missing;
    omitSections("### A one\nx\n", {"A", "Gone"}, &missing);
    EXPECT_EQ(missing, juce::StringArray{"Gone"});
}

// The drift signal: when a new production prompt is vendored and renames or drops one of these sections, this fails
// and names it; review compactOmittedHeadings() against the new prompt.
TEST(LocalPromptTrimTest, EveryOmittedHeadingStillExistsInTheVendoredPrompt) {
    juce::StringArray missing;
    omitSections(vendoredPrompt(), compactOmittedHeadings(), &missing);
    EXPECT_TRUE(missing.isEmpty()) << missing.joinIntoString(", ").toStdString();
}

TEST(LocalPromptTrimTest, CompactIsMuchShorterAndKeepsTheCoreRules) {
    const juce::String full = vendoredPrompt();
    const juce::String compact = omitSections(full, compactOmittedHeadings());
    EXPECT_LT(compact.length(), full.length() * 2 / 3);
    for (const char* kept : {"### What you produce", "### Patch rules", "### Tracks that play the patch",
                             "### Order of application", "### Example 4", "### Response format"})
        EXPECT_TRUE(compact.contains(kept)) << kept;
    for (const auto& heading : compactOmittedHeadings())
        EXPECT_FALSE(compact.contains("### " + heading)) << heading;
}
