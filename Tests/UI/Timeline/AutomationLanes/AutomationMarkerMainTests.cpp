// The automation-lane marker against a real MainComponent: adding a lane through the app's own path makes the card's
// knob report itself automated (tooltip "Automated: ..."), and removing the lane -- or undoing the add -- clears it.

#include "../../Graph/GraphEditor/GraphEditorTestHelpers.h"
#include "AI/AIProvider.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

class QuietProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "Quiet"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"Model"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
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
    juce::String model = "Model";
    int requestTimeoutMs = 240000;
};

void resetBottomDockVisibleKey() {
    juce::ApplicationProperties props;
    props.setStorageParameters(synth::userSettingsOptions());
    if (auto* s = props.getUserSettings()) {
        s->setValue("bottomDockVisible", "0");
        s->saveIfNeeded();
    }
}

class AutomationMarkerMainTest : public ::testing::Test {
protected:
    void SetUp() override { resetBottomDockVisibleKey(); }
    void TearDown() override { resetBottomDockVisibleKey(); }
};

juce::Slider* knobNamed(ModuleComponent& card, const juce::String& id) {
    for (auto* child : card.getChildren())
        if (child->getComponentID() == id)
            return dynamic_cast<juce::Slider*>(child);
    return nullptr;
}

} // namespace

TEST_F(AutomationMarkerMainTest, AddingAndRemovingALaneShowsAndClearsTheCardMarker) {
    MainComponent mc(std::make_unique<QuietProvider>());
    auto& graph = mc.getAudioEngine().getGraph();
    auto node = graph.addNode(synth::AIStateMapper::createModule("Filter"));
    const auto uuid = juce::Uuid().toDashedString();
    node->properties.set("uuid", uuid);
    dynamic_cast<ModuleBase*>(node->getProcessor())->setNodeUuid(uuid);

    auto& editor = mc.getGraphEditor();
    editor.updateComponents();
    sizeModuleComponents(editor);
    auto* card = findModuleComp(editor, node->getProcessor());
    ASSERT_NE(card, nullptr);
    auto* cutoff = knobNamed(*card, "Cutoff");
    ASSERT_NE(cutoff, nullptr);

    card->timerCallback();
    EXPECT_FALSE(card->isAutomatedMarkerShownForTest(cutoff));

    mc.automateParameter(node->nodeID, "cutoff"); // the right-click "Automate" path
    card->timerCallback();
    EXPECT_TRUE(card->isAutomatedMarkerShownForTest(cutoff));
    EXPECT_TRUE(cutoff->getTooltip().contains("Automated: Cutoff")) << cutoff->getTooltip();

    const auto* lane = mc.getTimelineDoc().getLaneForParam(uuid, "cutoff");
    ASSERT_NE(lane, nullptr);
    ASSERT_TRUE(mc.getTimelineDoc().removeLane(lane->id));
    card->timerCallback();
    EXPECT_FALSE(card->isAutomatedMarkerShownForTest(cutoff));
    EXPECT_FALSE(cutoff->getTooltip().contains("Automated"));
}
