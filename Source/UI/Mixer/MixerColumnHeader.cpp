// MixerColumnHeader.cpp -- the constructor and destructor of MixerColumnHeader.h, out of line so the header does not
// pull RestoredColourGlide.h into every file that shows a mixer column.
#include "UI/Mixer/MixerColumnHeader.h"
#include "UI/Layout/KeyGlide/RestoredColourGlide.h"

namespace synth::ui {

MixerColumnHeader::MixerColumnHeader() {
    colourGlide_ = std::make_unique<RestoredColourGlide>(*this);
    setInterceptsMouseClicks(true, false);
    addAndMakeVisible(nameLabel_);
    nameLabel_.setComponentID("mixerColumnHeaderName");
    nameLabel_.setJustificationType(juce::Justification::centredLeft);
    nameLabel_.setMinimumHorizontalScale(1.0f);
    // setRenameEnabled(true) below is what actually arms editing AND the tooltip/AX
    // help text together -- see that method's own comment for why the two must never diverge.
    setRenameEnabled(true);
    // The name label swallows the mouse, but the whole header is the drag handle of a reorderable
    // column -- so the header listens to the label's events too (see mouseDown() below).
    nameLabel_.addMouseListener(this, false);
    nameLabel_.setCursorSource(this);
    addAndMakeVisible(colourDot_);
    colourDotFade_.setShown(false); // faded in once setColour() gives it a colour
    colourDot_.onClick = [this] {
        if (colourDot_.consumeDragFlag())
            return; // the release of a header drag, not a click
        if (onColourClicked)
            onColourClicked(colourDot_.getScreenBounds());
    };
    colourDot_.dragForwarding = {[this](const juce::MouseEvent& e) { mouseDown(e.getEventRelativeTo(this)); },
                                 [this](const juce::MouseEvent& e) { mouseDrag(e.getEventRelativeTo(this)); },
                                 [this](const juce::MouseEvent& e) {
                                     mouseUp(e.getEventRelativeTo(this)); // may rebuild, and destroy, this header
                                     return true;
                                 },
                                 [this] { return reorderHooks.isDragging && reorderHooks.isDragging(); }};
    setColourEditable(false);
    colourGlide_->setOnDirty([this](juce::Rectangle<int>) {
        colourDot_.setColour(getDrawnColour());
        repaint();
    });

    addChildComponent(sourcesButton_);
    sourcesButton_.setIcon(synth::theme::Icon::MixerSources);
    sourcesButton_.setComponentID("mixerSourcesBadge");
    // A helper tip: it says who plays into the channel, which nothing else on screen does, so it keeps showing
    // when Preferences > "Show info tooltips" is off (docs/layout/animation.md#tooltips).
    synth::ui::markHelperTooltip(sourcesButton_);
    // Mostly information (its words are the tooltip and the screen-reader text); activating it selects the
    // channel on the canvas, the same as a click anywhere else on the header.
    sourcesButton_.onClick = [this] {
        if (onHeaderClicked)
            onHeaderClicked();
    };

    addChildComponent(pluginOffBadge_);
    pluginOffBadge_.setComponentID("mixerPluginOffBadge");
    pluginOffBadge_.onClick = [this] {
        if (onPluginOffClicked)
            onPluginOffClicked();
    };
    pluginOffBadge_.setOnFade([this] { resized(); });

    addAndMakeVisible(editOnCanvasButton_);
    editOnCanvasButton_.setComponentID("mixerEditOnCanvas");
    editOnCanvasButton_.onClick = [this] {
        if (onEditOnCanvasClicked)
            onEditOnCanvasClicked();
    };
    refreshEditOnCanvasTexts();

    nameLabel_.onTextChange = [this] {
        // name_ deliberately NOT updated here -- it stays the last value an external
        // setDisplayName() committed until either a rebuild calls setDisplayName() again with
        // whatever the edit actually produced, or the edit is refused and restoreDisplayName()
        // below puts the label's own text back to it. Firing onNameEdited with the label's live
        // text (not name_) is what lets the handler tell "edited to X" apart from "still Y".
        if (onNameEdited)
            onNameEdited(nameLabel_.getText());
    };
}

MixerColumnHeader::~MixerColumnHeader() = default;

void MixerColumnHeader::setColour(juce::Colour colour, bool restored) {
    if (colour_ == colour)
        return;
    // The dot's presence/absence shifts the name's bounds too, so layout must redo, not
    // just repaint. An undo or redo (`restored`) eases a colour change between two visible colours; any other
    // change lands at once.
    const auto was = colour_;
    colour_ = colour;
    colourGlide_->note(0, was, colour, restored && was.getAlpha() > 0 && colour.getAlpha() > 0);
    colourDot_.setColour(getDrawnColour());
    colourDotFade_.setShown(colour.getAlpha() > 0);
    resized();
    repaint();
}

juce::Colour MixerColumnHeader::getDrawnColour() const { return colourGlide_->drawn(0, colour_); }

void MixerColumnHeader::glideColourFrom(juce::Colour from) { colourGlide_->glideFrom(0, from, colour_); }

} // namespace synth::ui
