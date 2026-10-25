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
    /** For a field that can follow the song key: following reads "Song key, middle dot, <key>" muted, own reads "<key>,
     * middle dot, own". Sets the tooltip and the accessible title ("<what>, following song key"). Never called = no
     * follow state. */
    void setFollowing(bool following, const juce::String& what = "Key");
    bool isFollowing() const noexcept { return following_.value_or(false); }

    /** The user asked to open the picker; unset = the field is inert. */
    std::function<void()> onOpen;

    void showPopup() override;
    bool keyPressed(const juce::KeyPress& key) override;

protected:
    void lookAndFeelChanged() override;

private:
    void refreshText();
    void refreshFollowColour();

    std::optional<MusicalScale> key_;
    juce::String noKeyText_{"No key"};
    std::optional<bool> following_;

    JUCE_DECLARE_NON_COPYABLE(KeyField)
};

} // namespace synth::ui
