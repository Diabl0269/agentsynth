// Concern: the test-mode assistant answer. It calls AIChatComponent::showAssistantAnswerForTesting, the same path a
// provider's reply takes after the network (plan extraction, preview, card), minus the network.

#include "TestMode/TestModeAiAnswer.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeComponents.h"

namespace synth::testmode {

namespace {

// {text: "<plan or patch JSON>"}; the AI panel must be showing for the card to be on screen.
StepResult cmdAiAnswer(const juce::var& args, Scratch&, int) {
    auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow());
    auto* mc = window != nullptr ? dynamic_cast<MainComponent*>(window->getContentComponent()) : nullptr;
    if (mc == nullptr)
        return StepResult::failed("no main window");
    const auto text = args.getProperty("text", {}).toString();
    if (text.isEmpty())
        return StepResult::failed("aiAnswer takes {text: \"<plan or patch JSON>\"}");
    mc->getAiChatComponent().showAssistantAnswerForTesting(text);
    auto* o = new juce::DynamicObject();
    o->setProperty("shown", true);
    return StepResult::done(juce::var(o));
}

} // namespace

void registerAiAnswerCommands(CommandTable& table) { table.add("aiAnswer", {cmdAiAnswer}); }

} // namespace synth::testmode
