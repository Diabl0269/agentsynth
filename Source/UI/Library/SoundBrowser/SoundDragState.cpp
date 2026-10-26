// SoundDragState.cpp -- the one app-wide "a sound is being dragged" flag and its listeners.
#include "UI/Library/SoundBrowser/SoundDragState.h"

namespace synth::ui {

namespace {
struct State {
    bool active = false;
    bool shapeActive = false;
    synth::shapes::ShapeKind shapeKind = synth::shapes::ShapeKind::Filter;
    juce::String shapeName;
    juce::File file;
    juce::File loopFile; // the latest drag's file and target; kept after the drag ends
    synth::soundbrowser::LoopTarget loopTarget;
    juce::ListenerList<SoundDragState::Listener> listeners;
};

// Never destroyed: a component torn down at process exit can still remove itself.
State& state() {
    static State* instance = new State();
    return *instance;
}
} // namespace

void SoundDragState::begin(const juce::File& file, const synth::soundbrowser::LoopTarget& loop) {
    state().loopFile = file;
    state().loopTarget = loop;
    state().active = true;
    state().shapeActive = false;
    state().file = file;
    state().listeners.call([](Listener& l) { l.soundDragChanged(); });
}

void SoundDragState::beginShape(synth::shapes::ShapeKind kind, const juce::String& name) {
    state().active = false;
    state().file = juce::File();
    state().shapeActive = true;
    state().shapeKind = kind;
    state().shapeName = name;
    state().listeners.call([](Listener& l) { l.soundDragChanged(); });
}

void SoundDragState::end() {
    if (!state().active && !state().shapeActive)
        return;
    state().active = false;
    state().shapeActive = false;
    state().shapeName = {};
    state().file = juce::File();
    state().listeners.call([](Listener& l) { l.soundDragChanged(); });
}

bool SoundDragState::isActive() { return state().active; }

bool SoundDragState::isShapeActive() { return state().shapeActive; }

synth::shapes::ShapeKind SoundDragState::shapeKind() { return state().shapeKind; }

juce::String SoundDragState::shapeName() { return state().shapeName; }

juce::File SoundDragState::file() { return state().file; }

synth::soundbrowser::LoopTarget SoundDragState::loopTargetFor(const juce::File& file) {
    return file == state().loopFile ? state().loopTarget : synth::soundbrowser::LoopTarget{};
}

void SoundDragState::addListener(Listener* listener) { state().listeners.add(listener); }

void SoundDragState::removeListener(Listener* listener) { state().listeners.remove(listener); }

} // namespace synth::ui
