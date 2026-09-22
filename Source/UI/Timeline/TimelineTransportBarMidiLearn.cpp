// Right-click MIDI Learn on the transport bar's four glyph buttons (FRO133,
// docs/control/midi-remote-ui.md#right-click-midi-learn--coverage). Unlike the module card
// (FRO130) and the mixer column (FRO133, MixerColumnMidiLearn.cpp), every target here is an
// ACTION target (docs/control/midi-remote.md#action-targets), not a graph parameter -- the bar
// stays graph-free (Source/UI/CLAUDE.md's track-header precedent: "never sees the graph"), and it
// knows only the four fixed ShortcutManager action ids its own glyphs invoke. That is also why
// there is no per-instance registry here the way ModuleComponent/MixerColumnComponent need one:
// the (component -> target) mapping is fixed at construction (one glyph, one action id) rather
// than built up by a registerMidiLearnable() call per control, so actionIdForGlyph()/
// glyphButtonForAction() are a plain switch instead of a std::vector<Entry>.
//
// Learn/forget/query route through NEW callbacks (onMidiLearnRequested(actionId) etc.) rather than
// GraphEditor's node-keyed ones, wired by MainComponent straight to
// MidiLearnController::armAction()/forgetAction()/queryActionMappings() -- see
// MidiLearnController.h's own comment on why an action assignment is a GLOBAL profile edit, not a
// project-doc one.

#include "TimelineTransportBar.h"
#include "UI/MidiRemote/MidiLearnMenu.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// ============================================================================
// Glyph <-> action id
// ============================================================================

juce::String TimelineTransportBar::actionIdForGlyph(GlyphButton::Glyph glyph) {
    switch (glyph) {
    case GlyphButton::Glyph::PlayStop:
        return "transportTogglePlayStop";
    case GlyphButton::Glyph::Record:
        return "transportRecord";
    case GlyphButton::Glyph::Loop:
        return "transportToggleLoop";
    case GlyphButton::Glyph::Metronome:
        return "transportToggleMetronome";
    }
    return {};
}

TimelineTransportBar::GlyphButton* TimelineTransportBar::glyphButtonForAction(const juce::String& actionId) {
    if (actionId == actionIdForGlyph(GlyphButton::Glyph::PlayStop))
        return &playStopButton_;
    if (actionId == actionIdForGlyph(GlyphButton::Glyph::Record))
        return &recordButton_;
    if (actionId == actionIdForGlyph(GlyphButton::Glyph::Loop))
        return &loopButton_;
    if (actionId == actionIdForGlyph(GlyphButton::Glyph::Metronome))
        return &metronomeButton_;
    return nullptr;
}

juce::String TimelineTransportBar::displayNameForGlyph(GlyphButton::Glyph glyph) {
    switch (glyph) {
    case GlyphButton::Glyph::PlayStop:
        return "Play/Stop";
    case GlyphButton::Glyph::Record:
        return "Record";
    case GlyphButton::Glyph::Loop:
        return "Loop";
    case GlyphButton::Glyph::Metronome:
        return "Metronome";
    }
    return {};
}

// ============================================================================
// Right-click dispatch
// ============================================================================

void TimelineTransportBar::mouseDown(const juce::MouseEvent& e) {
    auto* button = dynamic_cast<GlyphButton*>(e.eventComponent);
    if (button == nullptr || !e.mods.isPopupMenu())
        return; // not one of the four glyph buttons, or a left-click (the button's own business)

    if (!onMidiLearnRequested)
        return; // headless build, or the MIDI Remote host isn't wired

    const juce::String actionId = actionIdForGlyph(button->getGlyph());
    juce::String label;
    if (onQueryMidiMappingsForActions) {
        const auto mappings = onQueryMidiMappingsForActions();
        const auto found = mappings.find(actionId);
        if (found != mappings.end())
            label = found->second;
    }

    juce::Component::SafePointer<TimelineTransportBar> safeThis(this);

    synth::ui::midilearn::MenuContent content;
    content.targetName = displayNameForGlyph(button->getGlyph());
    content.mappingLabel = label;
    content.learn = [safeThis, actionId] {
        if (safeThis != nullptr && safeThis->onMidiLearnRequested)
            safeThis->onMidiLearnRequested(actionId);
    };
    content.forget = [safeThis, actionId] {
        if (safeThis != nullptr && safeThis->onMidiForgetRequested)
            safeThis->onMidiForgetRequested(actionId);
    };
    if (onEditMidiAssignmentRequested) {
        content.editAssignment = [safeThis, actionId] {
            if (safeThis != nullptr && safeThis->onEditMidiAssignmentRequested)
                safeThis->onEditMidiAssignmentRequested(actionId);
        };
    }

    juce::PopupMenu menu;
    synth::ui::midilearn::appendMidiLearnMenuItems(menu, content);
    if (menu.getNumItems() == 0)
        return;
    showContextMenuHook_(menu);
}

// ============================================================================
// Armed state (the breathing outline)
// ============================================================================

void TimelineTransportBar::setMidiLearnArmedAction(const juce::String& actionId) {
    if (midiLearnArmedActionId_ == actionId)
        return;
    if (auto* was = glyphButtonForAction(midiLearnArmedActionId_))
        repaint(was->getBounds().expanded(2));
    midiLearnArmedActionId_ = actionId;
    midiLearnArmedSinceMs_ = juce::Time::getMillisecondCounterHiRes();
    if (auto* now = glyphButtonForAction(midiLearnArmedActionId_))
        repaint(now->getBounds().expanded(2));
}

// ============================================================================
// Test/inspection
// ============================================================================

juce::String TimelineTransportBar::findMidiLearnableActionForTest(const juce::Component* component) const {
    if (auto* button = dynamic_cast<const GlyphButton*>(component))
        return actionIdForGlyph(button->getGlyph());
    return {};
}

bool TimelineTransportBar::isMidiLearnBadgeMappedForTest(const juce::Component* component) const {
    auto* button = dynamic_cast<const GlyphButton*>(component);
    if (button == nullptr)
        return false;
    const auto found = midiLearnMappedBadges_.find(button->getGlyph());
    return found != midiLearnMappedBadges_.end() && found->second;
}

// ============================================================================
// Badges + armed outline (paint)
// ============================================================================

void TimelineTransportBar::refreshMidiLearnBadges() {
    if (!onQueryMidiMappingsForActions)
        return;
    const auto mappings = onQueryMidiMappingsForActions();
    bool changed = false;
    for (auto glyph : {GlyphButton::Glyph::PlayStop, GlyphButton::Glyph::Record, GlyphButton::Glyph::Loop,
                       GlyphButton::Glyph::Metronome}) {
        const bool mapped = mappings.find(actionIdForGlyph(glyph)) != mappings.end();
        auto& cached = midiLearnMappedBadges_[glyph];
        if (cached == mapped)
            continue;
        cached = mapped;
        changed = true;
    }
    if (changed)
        repaint();
}

void TimelineTransportBar::paintMidiLearnOverlays(juce::Graphics& g) {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const juce::Colour badgeColour = lf != nullptr ? lf->getTheme().colors.midiMapped : juce::Colour(0xffB48EF5);
    const juce::Colour armedColour = lf != nullptr ? lf->getTheme().colors.accent : juce::Colour(0xff00D1FF);

    for (const auto& [glyph, mapped] : midiLearnMappedBadges_) {
        if (!mapped)
            continue;
        if (auto* button = glyphButtonForAction(actionIdForGlyph(glyph)))
            synth::ui::midilearn::paintMidiMappedBadge(g, button->getBounds(), badgeColour);
    }

    if (midiLearnArmedActionId_.isEmpty())
        return;
    if (auto* button = glyphButtonForAction(midiLearnArmedActionId_))
        synth::ui::midilearn::paintMidiLearnArmedOutline(g, button->getBounds(), armedColour, midiLearnArmedSinceMs_);
}

} // namespace synth::ui
