#include "AI/PlanPreset.h"
#include "AIChatComponent.h"
#include "PatchSaveLocation.h"
#include "ProjectBundle.h"
#include "UI/Graph/CardLayoutEditor/PresetNamePrompt.h"

namespace synth {

// Concern: the edit-plan card's Save as preset (docs/ai/plan-preset.md). The name comes from the app's Save preset
// window (the one the card-layout editors use), the file from synth::planpreset: the plan's patch part, checked
// untrusted, written as a plain `.json` preset into the patch folder. Nothing here touches the live graph.
void AIChatComponent::savePlanAsPreset(const juce::String& planJson, std::function<void(const juce::String&)> done) {
    auto ask = presetNamePrompt ? presetNamePrompt : [](std::function<void(const juce::String&)> onName) {
        synth::ui::promptForPresetName(std::move(onName));
    };
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    ask([safeThis, planJson, done = std::move(done)](const juce::String& name) {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;
        const juce::File folder =
            self->presetFolder
                ? self->presetFolder()
                : synth::PatchSaveLocation::globalDirectory(synth::ProjectBundle::getDefaultProjectsDirectory());
        juce::File written;
        const auto result = synth::planpreset::saveAs(folder, name, planJson, written);
        juce::Logger::writeToLog("Edit plan save as preset: " + (result.ok ? written.getFileName() : result.message));
        if (!result.ok) {
            // A save that does nothing and says nothing reads as a broken button; same as a refused Apply.
            self->messages.push_back({"assistant", "Could not save this preset: " + result.message, ""});
            juce::MessageManager::callAsync([safeThis] {
                if (auto* chat = safeThis.getComponent())
                    chat->updateChatDisplay();
            });
            if (done)
                done({});
            return;
        }
        for (auto& message : self->messages)
            if (message.planJson == planJson)
                message.planPresetSaved = true;
        const auto saved = written.getFileNameWithoutExtension();
        if (self->onPresetSaved)
            self->onPresetSaved(saved);
        if (done)
            done(saved);
    });
}

void AIChatComponent::showAssistantAnswerForTesting(const juce::String& content, int tookMs) {
    juce::String prose;
    const juce::var plan = extractEditPlan(content, /*wholeResponseIsJson=*/true, prose);
    messages.push_back({"assistant", prose.trim(), plan.isVoid() ? juce::String() : juce::JSON::toString(plan)});
    messages.back().responseMs = tookMs;
    attachPlanPreview(messages.back(), plan);
    if (tookMs >= 0)
        saveCurrentConversationLocally();
    updateChatDisplay();
}

} // namespace synth
