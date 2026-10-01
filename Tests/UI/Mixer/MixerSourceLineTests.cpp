// MixerSourceLineTests.cpp (docs/mixer/panel.md#what-the-mixer-shows): the line under a strip header reads
// "From <sources>" with "From" muted, a tooltip and accessible description list every source, and the
// row is 0 px while no strip column in the panel has a source to name -- yet faders stay level across
// columns when some do. A real off-screen MainComponent and its mixer panel.
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerMasterColumn.h"
#include <gtest/gtest.h>

namespace {

class MockProviderMSL : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockMSL"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

struct SourceLineRig {
    BottomDockActiveTabResetGuardMDT resetGuard;
    MainComponent mc{std::make_unique<MockProviderMSL>()};
    synth::ui::MixerPanelComponent* panel = nullptr;

    explicit SourceLineRig(int tracks) {
        mc.setSize(1400, 900);
        mc.newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc.simulateAddAudioTrackClick();
        panel = &mc.getBottomDock().getMixerPanel();
        panel->setSize(1400, 320);
        panel->rebuild();
    }

    // Tracks 0 and 2 now feed one strip, so that strip's source differs from its own name.
    juce::String thirdTrackOwnBinding;

    void shareStripBetweenFirstAndLastTrack() {
        auto& doc = mc.getTimelineDoc();
        const auto& tracks = doc.getTracks();
        thirdTrackOwnBinding = tracks[2].bindingUuid;
        doc.setTrackBinding(tracks[2].id, tracks[0].bindingUuid);
        panel->rebuild();
    }
};

int faderTopOf(synth::ui::MixerColumnComponent& column) { return column.getFaderForTest().getY(); }

} // namespace

TEST(MixerSourceLineTest, TheRowCollapsesWhenNoColumnHasASourceToName) {
    SourceLineRig r(2);
    auto* a = r.panel->getStripColumnForTest(0);
    auto* b = r.panel->getStripColumnForTest(1);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_FALSE(a->hasSourceLine());
    EXPECT_FALSE(b->hasSourceLine());
    EXPECT_FALSE(r.panel->getSectionLayout().isSourceLineVisible());
    EXPECT_EQ(a->getSourceLineLabelForTest().getHeight(), 0) << "no gap under the names";
    EXPECT_FALSE(a->getSourceLineLabelForTest().isVisible());
    EXPECT_EQ(a->getHeaderForTest().getBottom(), a->getSectionViewportForTest(synth::ui::MixerSection::Inserts).getY());
    EXPECT_EQ(faderTopOf(*a), faderTopOf(*b));
}

TEST(MixerSourceLineTest, ASharedSourceReadsFromNamesAndListsEverySourceInTheTooltipAndDescription) {
    SourceLineRig r(3);
    r.shareStripBetweenFirstAndLastTrack();
    auto& doc = r.mc.getTimelineDoc();
    const auto first = doc.getTracks()[0].name;
    const auto last = doc.getTracks()[2].name;

    auto* shared = r.panel->getStripColumnForTest(0);
    ASSERT_NE(shared, nullptr);
    auto& line = shared->getSourceLineLabelForTest();
    ASSERT_TRUE(shared->hasSourceLine());
    EXPECT_TRUE(line.isVisible());
    EXPECT_EQ(line.getHeight(), synth::ui::MixerSectionLayout::kSourceLineHeight);
    EXPECT_EQ(line.getText(), "From " + first + ", " + last);
    const auto words = "Plays into this channel: " + first + ", " + last;
    EXPECT_EQ(line.getTooltip(), words);
    EXPECT_EQ(line.getDescription(), words);
    // The AX name is the visible text (createAccessibilityHandler directly: the live accessor needs a native peer).
    const auto handler = line.createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getTitle(), line.getText());
    EXPECT_EQ(handler->getDescription(), words);
}

TEST(MixerSourceLineTest, ColumnsWithoutASourceKeepAnAlignedBlankSoFadersStayLevel) {
    SourceLineRig r(3);
    r.shareStripBetweenFirstAndLastTrack();
    ASSERT_TRUE(r.panel->getSectionLayout().isSourceLineVisible());

    auto* shared = r.panel->getStripColumnForTest(0);
    ASSERT_NE(shared, nullptr);
    int blanks = 0;
    for (int i = 0; r.panel->getStripColumnForTest(i) != nullptr; ++i) {
        auto* column = r.panel->getStripColumnForTest(i);
        EXPECT_EQ(faderTopOf(*column), faderTopOf(*shared)) << "column " << i;
        EXPECT_EQ(column->getSourceLineLabelForTest().getHeight(), synth::ui::MixerSectionLayout::kSourceLineHeight)
            << "every column reserves the row";
        if (!column->hasSourceLine()) {
            ++blanks;
            EXPECT_TRUE(column->getSourceLineLabelForTest().getText().isEmpty());
            EXPECT_FALSE(column->getSourceLineLabelForTest().isVisible()) << "blank: nothing painted or hit";
        }
    }
    EXPECT_GE(blanks, 1);
    if (auto* master = r.panel->getMasterColumnForTest())
        EXPECT_EQ(master->getFaderForTest().getY(), faderTopOf(*shared)) << "Master stays level too";
}

TEST(MixerSourceLineTest, TheRowComesBackOnlyWhileSomeColumnNeedsIt) {
    SourceLineRig r(3);
    r.shareStripBetweenFirstAndLastTrack();
    EXPECT_TRUE(r.panel->getSectionLayout().isSourceLineVisible());

    auto& doc = r.mc.getTimelineDoc();
    ASSERT_TRUE(doc.setTrackBinding(doc.getTracks()[2].id, r.thirdTrackOwnBinding)); // back to its own strip
    r.panel->rebuild();
    EXPECT_FALSE(r.panel->getSectionLayout().isSourceLineVisible());
}
