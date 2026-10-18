#pragma once

#include "UI/Layout/NoteLengthMenu/NoteLengthPopup.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** The closed box of a note-length choice (docs/layout/note-length-menu.md): looks and behaves like a juce::ComboBox
 *  (it is one, so the card's layout, accessibility and parameter attachments treat it as one) but opens a
 *  NoteLengthPopup of S / T / D letters and chips instead of a menu. It shows the full name of the choice ("1/8T").
 *
 *  Fill it with setChoices(): one item per name, ids 1..N in list order, so a juce::ComboBoxParameterAttachment on a
 *  choice parameter maps item i to choice i. Names the popup does not know as note lengths ("Off", "Bar") become the
 *  extra chips in front of (or after) the numbers. Keyboard: the box is a Tab stop; Space, Enter or Down opens the
 *  popup, Left / Right step through the choices. */
class NoteLengthBox : public juce::ComboBox {
public:
    NoteLengthBox();
    ~NoteLengthBox() override;

    /** Replaces the items with `names`. `textPrefix` is written before each name in the closed box ("Grid " shows
     *  "Grid 1/8"); the names themselves stay as given. The selection stays on the same name when it is still there. */
    void setChoices(const juce::StringArray& names, const juce::String& textPrefix = {});
    const juce::StringArray& getChoices() const noexcept { return names_; }

    /** The name of the selected choice, empty when nothing is selected. */
    juce::String getCurrentName() const;
    /** Selects the choice called `name`; a name the list does not have changes nothing. */
    void setCurrentName(const juce::String& name, juce::NotificationType notification);

    /** A compact box has no chevron and a smaller, centred text, to fit a cell as narrow as a fader (40 px). */
    void setCompact(bool compact);
    bool isCompact() const noexcept;

    /** The tooltip of the letter button of a kind the list does not offer (it is disabled). */
    void setKindUnavailableTip(NoteKind kind, const juce::String& tip) {
        unavailableTip_[static_cast<size_t>(kind)] = tip;
    }
    /** Where the popup reads "Show info tooltips" from; optional. */
    void setApplicationProperties(juce::ApplicationProperties* properties) noexcept { appProperties_ = properties; }

    void showPopup() override;
    bool keyPressed(const juce::KeyPress& key) override;

    /** The open popup, or null. */
    NoteLengthPopup* getPopup() const noexcept { return popup_.get(); }

private:
    void popupClosed(bool restoreFocus);

    juce::StringArray names_;
    std::array<juce::String, 3> unavailableTip_;
    juce::ApplicationProperties* appProperties_ = nullptr;
    std::unique_ptr<NoteLengthPopup> popup_;

    JUCE_DECLARE_NON_COPYABLE(NoteLengthBox)
};

} // namespace synth::ui
