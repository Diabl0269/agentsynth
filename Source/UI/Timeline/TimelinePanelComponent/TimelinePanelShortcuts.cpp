// TimelinePanelShortcuts.cpp
//
// Panel-scoped keyboard shortcut dispatch: matchesAction() and keyPressed() -- edit-tool
// digits, snap/loop/follow-playhead toggles, loop-locator jumps, loop-the-selection.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling
// TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "ShortcutManager.h"

namespace synth::ui {

namespace {
// A bare (unmodified) keypress — the shape every one of this panel's own default bindings has.
juce::KeyPress plainKey(int keyCode) noexcept { return juce::KeyPress(keyCode, juce::ModifierKeys::noModifiers, 0); }

// The ShortcutManager action id that picks `tool`. Derived from editToolName() rather than written
// out as a second table, so adding a tool cannot leave a digit unbound here while EditTool.h,
// the button strip and the tooltips all already know about it — the ids in
// ShortcutManager::resetToDefaults() are exactly "timelineTool" + this name.
juce::String toolActionIdFor(synth::ui::EditTool tool) { return "timelineTool" + juce::String(editToolName(tool)); }

// Item: P (loop the selection) also ARMS looping by default; Preferences can turn the arming off
// so P only places the locators (Cubase's behaviour). Read at key time — no cached copy to drift.
constexpr const char* kTimelineLoopSelectionArmsPropertyKey = "timelineLoopSelectionArms";
} // namespace

bool TimelinePanelComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                           const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    const auto binding = shortcuts_->getBinding(actionId);
    // An invalid binding is "this action has no key": either the user cleared it, or this build's
    // ShortcutManager has never heard of the id (getBinding answers an unknown id with a
    // default-constructed KeyPress). Falling back to `fallback` here would resurrect a key the user
    // deliberately unbound, so it deliberately does not. keyPressMatches rather than == so a user
    // rebind onto a Shift-chorded symbol key survives the macOS peer delivering the SHIFTED
    // character as the key code (see ShortcutManager::keyPressMatches).
    return ShortcutManager::keyPressMatches(binding, key);
}

bool TimelinePanelComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && automationStripVisible_) {
        closeAutomationStrip();
        return true;
    }

    // T161: bare Down on the PANEL ROOT itself seeds keyboard focus into the track-header column.
    // Cmd+Shift+T / Tab land here (docs/shortcuts.md's Focus regions section — every region root
    // wants its own focus, deterministically), not on any row, so without this a keyboard-only user
    // could never reach a track header at all. Scoped to REAL focus being on THIS exact component
    // (never "focus is somewhere in the panel") so it can't steal an arrow key the clip lane or piano
    // roll haven't claimed for themselves — those two still own every other keystroke that reaches
    // this method by bubbling up from wherever real focus actually is.
    if (key.isKeyCode(juce::KeyPress::downKey) && juce::Component::getCurrentlyFocusedComponent() == this) {
        moveFocusedTrack(1);
        return true;
    }

    // Number keys pick a tool, BEFORE the letter keys below.
    //
    // With a ShortcutManager installed each digit is one rebindable action ("timelineToolSplit" and
    // friends), resolved through matchesAction — so an unset id has no key, and Ctrl+Shift+1 (the
    // grid command) can never be mistaken for a bare 1, because juce::KeyPress equality is exact on
    // modifiers.
    //
    // With NO manager (headless tests, embeddings with no settings store) the pre-shortcut behaviour
    // is kept verbatim: the digits come off editToolForKeyChar, command-modified digits are left
    // alone (a host/app menu shortcut owns those), and the digits EditTool.h reserves — 2, 6 and 9 —
    // return false and keep whatever meaning they have elsewhere. That fallback reads the TEXT
    // CHARACTER first, which is what a real keystroke carries on a layout where the digit needs a
    // modifier; the manager path cannot do that, since a modifier there is part of the binding.
    if (shortcuts_ != nullptr) {
        for (auto tool : kAllEditTools) {
            if (matchesAction(key, toolActionIdFor(tool), plainKey('0' + editToolKeyDigit(tool)))) {
                setActiveTool(tool);
                return true;
            }
        }
    } else if (!key.getModifiers().isCommandDown()) {
        const int typed = (int)key.getTextCharacter();
        if (const auto tool = editToolForKeyChar(typed != 0 ? typed : key.getKeyCode())) {
            setActiveTool(tool.value());
            return true;
        }
    }

    // Panel-scoped transport/snap keys. These fire when the key was NOT consumed by the focused
    // child (JUCE bubbles unhandled keys up the parent chain), so they cover every focus target
    // inside the timeline — track headers, the lanes, the roll (which consumes Q itself).

    // J = toggle grid magnetism (Cubase's snap key). Shares "timelineSnapToggle" with the roll: one
    // binding, one key, whichever surface has focus. Deliberately NOT Q any more — Q is Cubase's
    // quantise, which is what the roll uses it for, so one letter meant two verbs depending on
    // which timeline surface happened to have focus.
    if (matchesAction(key, "timelineSnapToggle", plainKey('j'))) {
        setSnapEnabled(!viewState_.snapEnabled);
        return true;
    }

    // L = toggle looping, keeping the existing bounds — exactly the transport bar's loop button.
    if (matchesAction(key, "timelineToggleLoop", plainKey('l')) && transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, !snap.looping);
        ruler_.repaint();
        return true;
    }

    // F = follow playhead on/off — the transport strip's follow button as a key, panel-scoped for
    // the same reason as Q: it has to work whichever timeline surface has focus, the roll included
    // (setFollowPlayheadEnabled already persists the choice and forwards the flag into the roll).
    if (matchesAction(key, "timelineFollowPlayheadToggle", plainKey('f'))) {
        setFollowPlayheadEnabled(!isFollowPlayheadEnabled());
        return true;
    }

    // Option+1 / Option+2 = park the cursor on the left / right loop locator.
    //
    // Surface-resolved, not a command: it acts on the timeline's own transport, and there is
    // nothing for it to do on any other surface. A DEGENERATE or unset span (end <= start, which is
    // also what "no locators yet" looks like) is a no-op that returns false, so the keystroke stays
    // available to whatever else might claim it rather than being silently swallowed.
    //
    // REACHABILITY, and it is the whole bug these keys shipped with: this method only runs when the
    // focused component is INSIDE this panel's subtree (JUCE bubbles an unhandled key up the parent
    // chain), and the only thing under this panel that takes keyboard focus is the clip lane area
    // (and the roll). Setting locators by dragging the RULER — the obvious way to do it — leaves
    // focus wherever it was, so the keystroke never reached here at all.
    // MainComponent::keyPressed forwards these two ids back to this panel as its last act for
    // exactly that reason; see its `forwardsToTimelinePanel` list.
    if (transport_ != nullptr) {
        const juce::ModifierKeys alt{juce::ModifierKeys::altModifier};
        const bool toStart = matchesAction(key, "timelineJumpToLocator1", juce::KeyPress('1', alt, 0));
        const bool toEnd = matchesAction(key, "timelineJumpToLocator2", juce::KeyPress('2', alt, 0));
        if (toStart || toEnd) {
            const auto snap = transport_->getPositionSnapshot();
            if (!(snap.loopEndPpq > snap.loopStartPpq))
                return false;
            transport_->locateBeat(toStart ? snap.loopStartPpq : snap.loopEndPpq);
            // The playhead overlay picks the new position up on the panel's next 10 Hz poll; the
            // ruler needs no repaint (the locators themselves did not move).
            return true;
        }
    }

    // P = loop the selection. With the roll open the "selection" is the edited clip; otherwise the
    // lane area already handles P itself when focused — this is the fallback for other focus
    // targets inside the panel (same span, same setLoop the lane's callback performs).
    if (matchesAction(key, "timelineLoopSelection", plainKey('p')) && transport_ != nullptr) {
        std::optional<std::pair<double, double>> span;
        if (pianoRoll_.isOpen() && doc_ != nullptr) {
            if (const auto* clip = doc_->getClip(pianoRoll_.getClipId()))
                span = std::make_pair(clip->startBeat, clip->startBeat + clip->lengthBeats);
        } else {
            span = clipLaneArea_.getSelectedClipSpan();
        }
        if (!span || !(span->second > span->first))
            return false;
        // Whether P also ARMS looping is a preference (default yes); off means "place the
        // locators, keep the current loop state" — Cubase's reading.
        bool arm = true;
        if (appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr)
            arm = appProperties_->getUserSettings()->getBoolValue(kTimelineLoopSelectionArmsPropertyKey, true);
        transport_->setLoop(span->first, span->second, arm || transport_->getPositionSnapshot().looping);
        ruler_.repaint();
        return true;
    }

    return false;
}

} // namespace synth::ui
