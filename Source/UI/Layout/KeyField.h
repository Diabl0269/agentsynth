#pragma once

#include "Music/MusicalScale.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** The closed box of a key choice: looks and behaves like a juce::ComboBox (a Tab stop; a click, Space, Return or Down
 *  opens) but has no items and opens whatever `onOpen` launches, the key picker. It shows the key ("D Dorian") or the
 *  no-key text. A mouse click never takes keyboard focus from the editing surface. */
class KeyField : public juce::ComboBox {
public:
    KeyField();

    /** Shows `key`, or the no-key text when it has no value. */
    void setKey(const std::optional<MusicalScale>& key);
    const std::optional<MusicalScale>& getKey() const noexcept { return key_; }
    /** What the field reads with no key; "No key" by default. */
    void setNoKeyText(const juce::String& text);

    /** The user asked to open the picker; unset = the field is inert. */
    std::function<void()> onOpen;

    void showPopup() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void refreshText();

    std::optional<MusicalScale> key_;
    juce::String noKeyText_{"No key"};

    JUCE_DECLARE_NON_COPYABLE(KeyField)
};

} // namespace synth::ui
