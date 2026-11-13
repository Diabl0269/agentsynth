#pragma once

#include "UI/Layout/IconButton.h"
#include "UI/Layout/SidePane/SidePane.h"
#include <juce_gui_basics/juce_gui_basics.h>

// SidePaneToggleButton.h (docs/layout/side-pane.md): the button at the left end of a tab's own top bar
// that shows or hides that tab's SidePane. Follows the pane it is bound to: on while the pane is open,
// hidden while the pane has no content. A bare synth::ui::IconButton: the window glyph's left strip fills
// while the pane is open (Glyph::SidePaneOpen) and every colour and the focus ring come from drawIconButton.
namespace synth::ui {

class SidePaneToggleButton
    : public IconButton
    , private juce::ChangeListener {
public:
    static constexpr int kWidth = 28;

    SidePaneToggleButton();
    ~SidePaneToggleButton() override;

    /** `pane` (nullable) must outlive this button or be unbound first. */
    void bind(SidePane* pane);
    /** The key text appended to the tooltip, e.g. "Cmd+Shift+B"; empty for none. */
    void setShortcutText(const juce::String& text);

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();

    SidePane* pane_ = nullptr;
    juce::String shortcutText_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidePaneToggleButton)
};

} // namespace synth::ui
