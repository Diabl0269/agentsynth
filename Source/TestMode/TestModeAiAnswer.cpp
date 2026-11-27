// Concern: the test-mode assistant answer. It calls AIChatComponent::showAssistantAnswerForTesting, the same path a
// provider's reply takes after the network (plan extraction, preview, card), minus the network.

#include "TestMode/TestModeAiAnswer.h"
#include "MainComponent/MainComponent.h"
#include "TestMode/TestModeComponents.h"

namespace synth::testmode {

namespace {

// {text: "<plan or patch JSON>", tookMs?: how long the answer took, saved with the chat}; the AI panel must be showing
// for the card to be on screen.
StepResult cmdAiAnswer(const juce::var& args, Scratch&, int) {
    auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow());
    auto* mc = window != nullptr ? dynamic_cast<MainComponent*>(window->getContentComponent()) : nullptr;
    if (mc == nullptr)
        return StepResult::failed("no main window");
    const auto text = args.getProperty("text", {}).toString();
    if (text.isEmpty())
        return StepResult::failed("aiAnswer takes {text: \"<plan or patch JSON>\"}");
    mc->getAiChatComponent().showAssistantAnswerForTesting(text, (int)args.getProperty("tookMs", -1));
    auto* o = new juce::DynamicObject();
    o->setProperty("shown", true);
    return StepResult::done(juce::var(o));
}

// The plan every songCard shows unless one is given: 6 sections, 5 tracks, 174 BPM in A minor.
const char* kDefaultSongPlan =
    R"({"plan":{"title":"Dark drum and bass","bpm":174,"key":"A minor","special":"",
    "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"},
                {"name":"Build","bars":16,"energy":3,"chords":"Am F","description":"rising"},
                {"name":"Drop","bars":32,"energy":5,"chords":"Am G","description":"full"},
                {"name":"Break","bars":16,"energy":2,"chords":"F","description":"pad"},
                {"name":"Drop 2","bars":32,"energy":5,"chords":"Am G","description":"full"},
                {"name":"Outro","bars":16,"energy":1,"chords":"Am","description":"fade"}],
    "tracks":[{"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"punchy","sections":["Intro","Drop"]},
              {"name":"Bass","role":"bass","instrument":"Synth","sound":"sub","sections":["Drop","Drop 2"]},
              {"name":"Pad","role":"chords","instrument":"Synth","sound":"warm","sections":["Intro","Break"]},
              {"name":"Lead","role":"lead","instrument":"Synth","sound":"bright","sections":["Drop","Drop 2"]},
              {"name":"FX","role":"fx","instrument":"Synth","sound":"noise","sections":["Build"]}],"lanes":[]},
    "price":{"units":5,"estimatedUsd":0.02},"steps":[{"id":"track:Kit","kind":"track","label":"Kit sound"}],"token":"tok"})";

// {plan?: "<plan reply data JSON>", phase: plan|building|built, steps?: 30, done?: 12, sentMelody?: "<clip name>"}; the
// AI panel must be showing.
StepResult cmdSongCard(const juce::var& args, Scratch&, int) {
    auto* window = dynamic_cast<juce::ResizableWindow*>(findMainWindow());
    auto* mc = window != nullptr ? dynamic_cast<MainComponent*>(window->getContentComponent()) : nullptr;
    if (mc == nullptr)
        return StepResult::failed("no main window");
    auto plan = args.getProperty("plan", {}).toString();
    if (plan.isEmpty())
        plan = kDefaultSongPlan;
    const auto phase = args.getProperty("phase", "plan").toString();
    const int steps = (int)args.getProperty("steps", 30);
    const int done = (int)args.getProperty("done", 12);
    if (!mc->getAiChatComponent().showSongCardForTesting(plan, phase, steps, done,
                                                         args.getProperty("sentMelody", {}).toString()))
        return StepResult::failed(
            "songCard takes {plan?: \"<plan data JSON>\", phase: plan|building|built, steps?, done?, sentMelody?}");
    auto* o = new juce::DynamicObject();
    o->setProperty("shown", true);
    return StepResult::done(juce::var(o));
}

} // namespace

void registerAiAnswerCommands(CommandTable& table) {
    table.add("aiAnswer", {cmdAiAnswer});
    table.add("songCard", {cmdSongCard});
}

} // namespace synth::testmode
