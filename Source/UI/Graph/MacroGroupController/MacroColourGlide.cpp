// MacroColourGlide.cpp -- see MacroColourGlide.h.
#include "UI/Graph/MacroGroupController/MacroColourGlide.h"

namespace synth::ui {

MacroColourGlide::MacroColourGlide(juce::Component& owner, AppUndoManager* undo, std::function<void()> onDirty)
    : glide_(owner) {
    glide_.setUndoManager(undo);
    glide_.setOnDirty([cb = std::move(onDirty)](juce::Rectangle<int>) {
        if (cb)
            cb();
    });
}

void MacroColourGlide::sync(const synth::MacroSet& macros, juce::Colour midiTint, juce::Colour cvTint) {
    const bool restored = glide_.takeRestore();
    std::map<juce::String, juce::Colour> now;
    const auto read = [&](const juce::String& id, juce::Colour colour) {
        now[id] = colour;
        if (const auto it = seen_.find(id); it != seen_.end())
            glide_.note(keyFor(id), it->second, colour, restored);
    };
    for (const auto& macro : macros.getAll()) {
        read(macro.id, macro.colour);
        for (const auto& port : macro.ports)
            read(macro.id + "/" + port.nodeUuid,
                 port.colour.value_or(port.kind == synth::MacroPortKind::Midi ? midiTint : cvTint));
    }
    for (const auto& [id, colour] : seen_)
        if (now.find(id) == now.end())
            glide_.forget(keyFor(id));
    seen_ = std::move(now);
}

juce::Colour MacroColourGlide::macroColour(const juce::String& macroId, juce::Colour resting) const {
    return glide_.drawn(keyFor(macroId), resting);
}

juce::Colour MacroColourGlide::portColour(const juce::String& macroId, const juce::String& portUuid,
                                          juce::Colour resting) const {
    return glide_.drawn(keyFor(macroId + "/" + portUuid), resting);
}

} // namespace synth::ui
