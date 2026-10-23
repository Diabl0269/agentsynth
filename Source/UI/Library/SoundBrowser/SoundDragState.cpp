// SoundDragState.cpp -- the one app-wide "a sound is being dragged" flag and its listeners.
#include "UI/Library/SoundBrowser/SoundDragState.h"

namespace synth::ui {

namespace {
struct State {
    bool active = false;
    juce::File file;
    juce::ListenerList<SoundDragState::Listener> listeners;
};

// Never destroyed: a component torn down at process exit can still remove itself.
State& state() {
    static State* instance = new State();
    return *instance;
}
} // namespace

void SoundDragState::begin(const juce::File& file) {
    state().active = true;
    state().file = file;
    state().listeners.call([](Listener& l) { l.soundDragChanged(); });
}

void SoundDragState::end() {
    if (!state().active)
        return;
    state().active = false;
    state().file = juce::File();
    state().listeners.call([](Listener& l) { l.soundDragChanged(); });
}

bool SoundDragState::isActive() { return state().active; }

juce::File SoundDragState::file() { return state().file; }

void SoundDragState::addListener(Listener* listener) { state().listeners.add(listener); }

void SoundDragState::removeListener(Listener* listener) { state().listeners.remove(listener); }

} // namespace synth::ui
