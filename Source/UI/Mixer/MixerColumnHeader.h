#pragma once

#include "UI/Layout/DragCursor.h"
#include "UI/Layout/NonModalLabel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

// MixerColumnHeader.h (docs/mixer/panel.md#what-the-mixer-shows): the colour swatch + name label +
// click-to-select-macro row shared by MixerColumnComponent, MixerDirectColumn and
// MixerMasterColumn, so the three column kinds don't triplicate the same paint code (root
// CLAUDE.md's "extract a real collaborator class" preference over copy-pasted paint()). Small
// enough to stay header-only, same as the plan's own budget for this file.
//
// The name is also double-click-to-rename in place -- nameLabel_ reuses the exact
// juce::Label(false, true, false) + onTextChange pattern TimelineTrackHeaderComponent::nameLabel_
// already established (Source/UI/Timeline/TimelineTrackHeaderComponent.cpp), not a bespoke text
// editor, so the two only "double-click a name to rename it" surfaces in the app behave alike. Same
// accepted trade-off that file's own comment documents: a click landing ON the label is consumed by
// it (never reaches mouseUp() below), so onHeaderClicked's "select on canvas" only fires for a click
// elsewhere in the header. What committing a rename actually MEANS (rename the boxing macro, or the
// strip's own persisted name) is MixerColumnComponent's call, via onNameEdited -- this class only
// hosts the edit gesture.

namespace synth::ui {

/** A label that shows another component's cursor, so a child covering part of a drag handle still reads as one. */
class CursorDelegatingLabel : public synth::ui::NonModalLabel {
public:
    void setCursorSource(juce::Component* source) noexcept { cursorSource_ = source; }
    juce::MouseCursor getMouseCursor() override {
        return cursorSource_ != nullptr ? cursorSource_->getMouseCursor() : juce::Label::getMouseCursor();
    }

private:
    juce::Component::SafePointer<juce::Component> cursorSource_;
};

class MixerColumnHeader
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    MixerColumnHeader() {
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

    /** An unset (default-constructed, alpha 0) colour paints no swatch -- Direct's header has no
     *  colour of its own (docs/mixer/panel.md#what-the-mixer-shows). */
    void setColour(juce::Colour colour) {
        if (colour_ == colour)
            return;
        // The swatch presence/absence shifts nameBounds() too, so layout must redo, not
        // just repaint.
        colour_ = colour;
        resized();
        repaint();
    }

    // Named setDisplayName/getDisplayName, not setName/getName -- juce::Component already declares
    // a virtual setName()/getName() pair for its OWN (debug/accessibility) component name, and
    // shadowing it with an unrelated meaning is exactly the kind of trap
    // -Winconsistent-missing-override exists to catch.
    void setDisplayName(const juce::String& name) {
        if (name_ == name)
            return;
        name_ = name;
        // dontSendNotification -- an externally-driven refresh (a rebuild, or restoring the
        // pre-edit name after a refused rename) must never re-fire onTextChange/onNameEdited; only
        // the user's own edit does that, from the constructor's lambda above.
        nameLabel_.setText(name_, juce::dontSendNotification);
        nameLabel_.setTitle(name_ + " name");
    }
    juce::String getDisplayName() const { return name_; }

    /** Fires with whatever text the user left in the label when an inline rename ends (Return, or
     *  focus lost while editing) -- MixerColumnComponent decides what that MEANS (see the class
     *  comment) and, if it refuses the edit, calls restoreDisplayName() below to put the label's
     *  text back to the last committed name; this class never reverts on its own. Left null (the
     *  default) for a header nothing can rename (Direct/Master today). */
    std::function<void(juce::String)> onNameEdited;

    /** Puts the label's text back to the last name setDisplayName() actually committed, discarding
     *  whatever the user just typed -- the refusal half of onNameEdited's contract (a macro commit
     *  left empty, or a column with no strip/macro behind it at all). Unlike setDisplayName(), this
     *  always writes the label even when it already reads the same text, since the whole point is
     *  undoing a live edit the label is still showing. */
    void restoreDisplayName() { nameLabel_.setText(name_, juce::dontSendNotification); }

    /** docs/mixer/mixer.md#channels-follow-audio-not-tracks: a small link glyph next to the name when this strip is
     * linked to a track; `trackName` names it in the tooltip/AX text (empty reads "Linked to a track"). */
    void setLinkedTrack(bool linked, const juce::String& trackName) {
        if (linkedBadgeVisible_ == linked && linkedTrackName_ == trackName)
            return;
        linkedBadgeVisible_ = linked;
        linkedTrackName_ = trackName;
        badgesChanged();
    }

    /** A "BUS" badge in place of the link glyph when this column is a group/send bus -- a bus has no track
     * to link to, so the two are mutually exclusive by construction (see docs/mixer/sends-and-buses.md). */
    void setBusBadgeVisible(bool visible) {
        if (busBadgeVisible_ == visible)
            return;
        busBadgeVisible_ = visible;
        badgesChanged();
    }

    /** The strips sending into this track channel (docs/mixer/sends-and-buses.md): a compact arrow + count
     *  badge when non-empty. Coexists with the link glyph -- receiving a send never unlinks a track channel. */
    void setReceivesFrom(const juce::StringArray& sources) {
        if (receivesFrom_ == sources)
            return;
        receivesFrom_ = sources;
        badgesChanged();
    }

    /** Makes `label` (a sibling of the header, e.g. the source line) part of the drag handle: it shows the
     *  header's cursor and forwards its mouse events here. */
    void adoptHandleLabel(CursorDelegatingLabel& label) {
        label.setCursorSource(this);
        label.addMouseListener(this, false);
    }

    /** Whether double-click-to-rename is armed at all. Direct and Master have no
     *  ChannelStripModule name (Direct has no node; Master is a MasterModule) for a rename to write
     *  to, so MixerDirectColumn/MixerMasterColumn turn this off right after their fixed
     *  setDisplayName() call -- true (the default) is MixerColumnComponent's every Strip/Bus
     *  column. Also gates the tooltip -- VoiceOver reads a juce::TooltipClient's tooltip as
     *  the label's accessible help text, so a disabled column must never still offer "Double-click
     *  to rename" (Direct/Master's AX help previously said so even though the label ignores the
     *  gesture). */
    void setRenameEnabled(bool enabled) {
        nameLabel_.setEditable(false, enabled, false);
        nameLabel_.setTooltip(enabled ? "Double-click to rename this channel" : juce::String());
    }

    /** Test seam: the real juce::Label a double-click opens an editor on -- a test drives it with
     *  Label's own mouseDoubleClick()/showEditor()/getCurrentTextEditor()/hideEditor(), exactly the
     *  gestures a live double-click and Return key produce, rather than calling setDisplayName()
     *  and skipping the edit gesture entirely. */
    juce::Label& getNameLabelForTest() noexcept { return nameLabel_; }

    /** Fires on a click anywhere in the header background -- docs/mixer/panel.md#what-the-mixer-shows's "clicking a
     * column selects its macro on the canvas". Left null (the default) for Direct/Master, which have no macro of their
     * own to select. */
    std::function<void()> onHeaderClicked;

    /** Fires on a right-click release over the header (docs/mixer/panel.md#side-pane-zones-and-visibility's pin menu).
     * Left null, a right-click behaves like any other click. */
    std::function<void(const juce::MouseEvent&)> onContextMenu;

    juce::Colour getColour() const noexcept { return colour_; }

    void paint(juce::Graphics& g) override {
        const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const auto surface = laf != nullptr ? laf->getTheme().colors.surfaceHi : juce::Colour(0xff232833);
        const auto text = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);
        const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

        g.setColour(surface);
        g.fillRect(getLocalBounds());

        // a 2px stripe in the same colour across the top edge, so the channel's track colour reads at a glance
        // even when the small swatch is skimmed past (Cubase-style).
        if (colour_.getAlpha() > 0) {
            g.setColour(colour_);
            g.fillRect(getLocalBounds().removeFromTop(2));
        }

        auto bounds = getLocalBounds().reduced(4);
        if (colour_.getAlpha() > 0) {
            auto swatch = bounds.removeFromLeft(10).reduced(0, 2);
            g.setColour(colour_);
            g.fillRoundedRectangle(swatch.toFloat(), 2.0f);
            bounds.removeFromLeft(4);
        }
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        if (busBadgeVisible_) {
            g.setColour(text.withAlpha(0.7f));
            g.drawText("BUS", bounds.removeFromRight(kBusBadgeWidth), juce::Justification::centred, false);
        } else if (linkedBadgeVisible_) {
            paintLinkGlyph(g, bounds.removeFromRight(kLinkBadgeWidth).toFloat(), accent);
        }
        if (!receivesFrom_.isEmpty()) {
            // A small right-pointing arrow (audio arriving) followed by the sender count.
            auto badge = bounds.removeFromRight(kReceivesBadgeWidth).toFloat();
            const auto arrow = badge.removeFromLeft(9.0f);
            g.setColour(text.withAlpha(0.7f));
            g.drawArrow({arrow.getX(), arrow.getCentreY(), arrow.getRight(), arrow.getCentreY()}, 1.2f, 5.0f, 4.0f);
            g.drawText(juce::String(receivesFrom_.size()), badge, juce::Justification::centred, false);
        }
        // The name itself is nameLabel_ (a real child component, so double-click can turn it into a
        // live text editor) -- resized() positions it over exactly this same reduction of bounds
        // (nameBounds() below), so the two must stay in step; nothing else is drawn into it here.
        nameLabel_.setColour(juce::Label::textColourId, text);
        nameLabel_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    }

    void resized() override { nameLabel_.setBounds(nameBounds()); }

    /** Drag-to-reorder hooks. All three unset (Direct, Master, a bus) leaves the header a plain
     *  click target. `onRelease` returns true when the gesture was a drag, which swallows the click. */
    struct ReorderHooks {
        std::function<void(const juce::MouseEvent&)> onGrab;
        std::function<void(const juce::MouseEvent&)> onDrag;
        std::function<bool(const juce::MouseEvent&)> onRelease;
    };
    ReorderHooks reorderHooks;

    /** The grab hand on hover and during the drag, but only for a header that can be dragged (hooks
     *  set); Direct, Master and pinned headers stay a plain click target. Read live, so it follows
     *  whenever the hooks are assigned. */
    juce::MouseCursor getMouseCursor() override {
        return reorderHooks.onGrab ? dragGrabCursor() : juce::Component::getMouseCursor();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (reorderHooks.onGrab && !e.mods.isPopupMenu())
            reorderHooks.onGrab(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (reorderHooks.onDrag && !e.mods.isPopupMenu())
            reorderHooks.onDrag(e);
    }

    // A press that never travels is a click. Deliberately not gated on juce::MouseEvent::
    // mouseWasClicked(), which reads the real MouseInputSource's own press-tracking state rather
    // than this event's own fields, and so does not answer correctly for a synthetic event built
    // by hand (a real test's mouseDown()/mouseUp() pair, not a live press) the way it does for a
    // genuine user gesture. A click that lands on the name label is that label's own (double-click
    // to rename), so only the header background selects the column.
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() && onContextMenu) {
            const auto menu = onContextMenu; // the pick may rebuild, and destroy, this header
            menu(e);
            return;
        }
        if (reorderHooks.onRelease) {
            const auto release = reorderHooks.onRelease; // the hook may rebuild, and destroy, this header
            if (release(e))
                return;
        }
        if (e.eventComponent != &nameLabel_ && onHeaderClicked)
            onHeaderClicked();
    }

private:
    // The header's content rect after the swatch and the linked/bus badge have each claimed their
    // space -- exactly what paint() leaves undrawn and nameLabel_ occupies. Kept as one function so
    // painting the badge and sizing the editable name label can never drift apart (paint() redoes
    // the same swatch/badge removal on its own copy of the bounds, to draw them).
    juce::Rectangle<int> nameBounds() const {
        auto bounds = getLocalBounds().reduced(4);
        if (colour_.getAlpha() > 0)
            bounds.removeFromLeft(10 + 4);
        if (busBadgeVisible_)
            bounds.removeFromRight(kBusBadgeWidth);
        else if (linkedBadgeVisible_)
            bounds.removeFromRight(kLinkBadgeWidth);
        if (!receivesFrom_.isEmpty())
            bounds.removeFromRight(kReceivesBadgeWidth);
        return bounds;
    }

    // The badges in words: the header's tooltip and its accessible description, one string so the
    // two can never disagree. Empty when no badge shows.
    juce::String badgeText() const {
        juce::StringArray parts;
        if (busBadgeVisible_)
            parts.add("Bus");
        else if (linkedBadgeVisible_)
            parts.add(linkedTrackName_.isNotEmpty() ? "Linked to track " + linkedTrackName_ : "Linked to a track");
        if (!receivesFrom_.isEmpty())
            parts.add("Receives sends from: " + receivesFrom_.joinIntoString(", "));
        return parts.joinIntoString(". ");
    }

    void badgesChanged() {
        const auto words = badgeText();
        setTooltip(words);
        setDescription(words);
        resized(); // a badge claims space out of nameBounds() too -- see setColour()'s own comment
        repaint();
    }

    // Two interlocking chain links, centred in `area`.
    static void paintLinkGlyph(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
        const auto centre = area.getCentre();
        const juce::Rectangle<float> link(0.0f, 0.0f, 7.0f, 4.5f);
        g.setColour(colour);
        g.drawRoundedRectangle(link.withCentre(centre.translated(-2.5f, 0.0f)), 2.2f, 1.2f);
        g.drawRoundedRectangle(link.withCentre(centre.translated(2.5f, 0.0f)), 2.2f, 1.2f);
    }

    static constexpr int kBusBadgeWidth = 26;
    static constexpr int kLinkBadgeWidth = 16;
    static constexpr int kReceivesBadgeWidth = 22;

    juce::Colour colour_; // alpha 0 by default -- see setColour()
    juce::String name_;
    CursorDelegatingLabel nameLabel_; // The name itself -- see the class comment's rename design
    bool linkedBadgeVisible_ = false;
    juce::String linkedTrackName_;
    bool busBadgeVisible_ = false;
    juce::StringArray receivesFrom_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerColumnHeader)
};

} // namespace synth::ui
