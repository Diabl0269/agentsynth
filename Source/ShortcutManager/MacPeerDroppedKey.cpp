#include "MacPeerDroppedKey.h"

namespace synth {

std::optional<juce::KeyPress> recoverKeyPressDroppedByMacPeer(int firstCharacter, int firstUnmodified,
                                                              juce::ModifierKeys modifiers) {
    // The peer only drops an event whose text is empty; anything else it delivers itself.
    if (firstCharacter != 0 || firstUnmodified <= 0)
        return std::nullopt;
    // NUL comes from Control only; without it the empty text is something else and not ours to rebuild.
    if (!modifiers.isCtrlDown())
        return std::nullopt;
    const int keyCode =
        static_cast<int>(juce::CharacterFunctions::toUpperCase(static_cast<juce::juce_wchar>(firstUnmodified)));
    return juce::KeyPress(keyCode, modifiers.withoutMouseButtons(), 0);
}

} // namespace synth
