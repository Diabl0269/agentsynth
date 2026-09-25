#include "AIChatComponent.h"

namespace synth {

// Concern: provider/model selection -- refreshing the available-models list and the
// hosted-mode notice / Patch-Arrange mode selector gates that follow a provider change.

void AIChatComponent::refreshModels() {
    // Provider identity (unlike its model list) is known synchronously right after
    // setProvider() — no need to wait for the fetch below to resolve. The mode selector's gates
    // don't read the provider (parity rule), but this is still a convenient known resync point
    // for owners that call refreshModels() after wiring things up.
    updateHostedModeNotice();
    refreshModeControls();

    // refreshModels() is called repeatedly over the component's lifetime (once at
    // construction with no provider yet, again after MainComponent installs one, and
    // again whenever Settings triggers a re-fetch) — never just once. Without this
    // clear(), the second call's addItem(..., 1) collides with whatever already holds
    // ID 1 (a previously listed model, or the "Error fetching models" placeholder from
    // a synchronous no-provider callback) and trips ComboBox's duplicate-ID jassert.
    modelPicker.clear(juce::dontSendNotification);
    modelPicker.addItem("Loading models...", 1);
    modelPicker.setSelectedId(1, juce::dontSendNotification);
    modelPicker.setEnabled(false);

    // SafePointer guard: a provider resolves this asynchronously (OllamaProvider hops back via
    // MessageManager::callAsync), so the callback can arrive after this component was destroyed,
    // e.g. when a MainComponent is torn down while a model fetch is still in flight.
    juce::Component::SafePointer<AIChatComponent> safeThis(this);
    aiService.fetchAvailableModels([safeThis](const juce::StringArray& models, bool success) {
        if (safeThis != nullptr)
            safeThis->applyFetchedModels(models, success);
    });
}

void AIChatComponent::applyFetchedModels(const juce::StringArray& models, bool success) {
    modelPicker.clear(juce::dontSendNotification);
    modelPicker.setEnabled(true);

    if (success && !models.isEmpty()) {
        for (int i = 0; i < models.size(); ++i) {
            modelPicker.addItem(models[i], i + 1);
        }

        // Select the saved model, current model, or default to first available
        juce::String savedModel = appProperties.getUserSettings()->getValue("aiModel", "");
        juce::String current = aiService.getCurrentModel();

        int index = -1;
        if (savedModel.isNotEmpty() && models.contains(savedModel)) {
            index = models.indexOf(savedModel);
        } else if (current.isNotEmpty()) {
            index = models.indexOf(current);
        }

        if (index != -1) {
            modelPicker.setSelectedId(index + 1, juce::dontSendNotification);
            aiService.setModel(models[index]);
        } else {
            modelPicker.setSelectedId(1, juce::dontSendNotification);
            aiService.setModel(models[0]);
        }
    } else if (success) {
        // Empty-but-successful is not a fetch failure — RemoteProvider's fetchAvailableModels()
        // always resolves this way, because the hosted service picks its own model server-side
        // (see RemoteProvider::fetchAvailableModels()'s doc comment). Showing "Error fetching
        // models" here would be actively misleading to every hosted-mode user.
        modelPicker.addItem("Model chosen automatically", 1);
        modelPicker.setSelectedId(1, juce::dontSendNotification);
        modelPicker.setEnabled(false);
    } else {
        modelPicker.addItem("Error fetching models", 1);
        modelPicker.setSelectedId(1, juce::dontSendNotification);
    }
}

void AIChatComponent::updateHostedModeNotice() {
    const bool hosted = aiService.isCurrentProviderHosted();
    if (hosted) {
        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
            hostedModeNotice.setColour(juce::Label::textColourId, lf->getTheme().colors.textMuted);
    }
    hostedModeNotice.setVisible(hosted);
    resized();
}

void AIChatComponent::refreshModeControls() {
    // PROVIDER-AGNOSTIC on purpose (the local/remote parity rule): arrange mode is served by
    // whichever transport the active provider has (AIIntegrationService::sendArrangeMessage), so
    // the selector's gate is the timeline preference alone — never the provider.
    // hasTimelineContext() rides along with the preference switch: with no live doc installed
    // there is nothing to summarise, validate against, or apply to, so offering Arrange would
    // only manufacture the "cannot be checked or applied" failure previewTimelineOps() reports.
    // In the real app the context is installed whenever the feature is on (MainComponent wires
    // both), so this is one gate in practice and a safety net in tests.
    const bool show = aiService.areTimelineToolsEnabled() && aiService.hasTimelineContext();

    if (modeSelector.isVisible() == show)
        return;

    modeSelector.setVisible(show);
    if (!show)
        modeSelector.setSelectedId(kModeSelectorPatchId, juce::dontSendNotification);
    resized();
}

} // namespace synth
