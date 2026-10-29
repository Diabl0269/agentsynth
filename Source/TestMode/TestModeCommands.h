#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// TestModeCommands.h: the commands the test-mode server runs (docs/development/test-mode.md#commands). Each runs on
// the message thread, one short step at a time: a command that has to wait answers `pending` and is called again a
// moment later, so the message thread is never blocked and timers and animations run in between.
namespace synth::testmode {

/** What one step of a command returned. */
struct StepResult {
    enum class State { done, pending, failed };
    State state = State::done;
    juce::var value;
    juce::String error;

    static StepResult done(juce::var v = {}) { return {State::done, std::move(v), {}}; }
    static StepResult pending() { return {State::pending, {}, {}}; }
    static StepResult failed(juce::String message) { return {State::failed, {}, std::move(message)}; }
};

/** State a command keeps between its steps. Message thread only. */
struct Scratch {
    juce::DynamicObject::Ptr vars = new juce::DynamicObject();
    juce::Component::SafePointer<juce::Component> target;
};

/** One call of a command; `attempt` counts from 0. */
using CommandStep = std::function<StepResult(const juce::var& args, Scratch& scratch, int attempt)>;

struct CommandSpec {
    CommandStep step;
    int defaultTimeoutMs = 30000;
};

class CommandTable {
public:
    /** Every command in the protocol: ping list click drag key getState screenshot openProject waitForIdle quit. */
    static CommandTable standard();

    void add(const juce::String& name, CommandSpec spec) { commands_[name] = std::move(spec); }
    const CommandSpec* find(const juce::String& name) const;
    juce::StringArray names() const;

private:
    std::map<juce::String, CommandSpec> commands_;
};

} // namespace synth::testmode
