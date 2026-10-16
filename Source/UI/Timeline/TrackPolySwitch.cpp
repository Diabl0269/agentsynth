// Concern: the track header's Mono | Poly switch (two joined toggle buttons).
#include "UI/Timeline/TrackPolySwitch.h"

namespace synth::ui {

TrackPolySwitch::TrackPolySwitch() {
    setComponentID("trackPolySwitch");
    mono_.setComponentID("trackMonoButton");
    poly_.setComponentID("trackPolyButton");
    mono_.setConnectedEdges(juce::Button::ConnectedOnRight);
    poly_.setConnectedEdges(juce::Button::ConnectedOnLeft);
    for (auto* button : {&mono_, &poly_}) {
        addAndMakeVisible(*button);
        button->setClickingTogglesState(false); // the graph is the truth; setState() sets the filled side
        button->setWantsKeyboardFocus(true);
        button->setMouseClickGrabsKeyboardFocus(false); // a click leaves focus on the row
    }
    mono_.onClick = [this] {
        if (onSwitch)
            onSwitch(false);
    };
    poly_.onClick = [this] {
        if (onSwitch)
            onSwitch(true);
    };
    setState(TrackPolyState::mono, {});
}

void TrackPolySwitch::setState(TrackPolyState state, const juce::String& trackName) {
    state_ = state;
    const juce::String name = trackName.isNotEmpty() ? trackName : juce::String("track");
    mono_.setToggleState(state == TrackPolyState::mono, juce::dontSendNotification);
    poly_.setToggleState(state == TrackPolyState::poly, juce::dontSendNotification);
    mono_.setTitle("Mono, " + name);
    poly_.setTitle("Poly, " + name);
    const juce::String half =
        state == TrackPolyState::mixed ? " Some of its modules are mono and some poly right now." : juce::String();
    mono_.setTooltip("Play one note at a time: switches the whole voice chain of " + name + " to mono." + half);
    poly_.setTooltip("Play several notes at once: switches the whole voice chain of " + name + " to poly." + half);
}

void TrackPolySwitch::resized() {
    auto bounds = getLocalBounds();
    mono_.setBounds(bounds.removeFromLeft(bounds.getWidth() / 2));
    poly_.setBounds(bounds);
}

} // namespace synth::ui
