// EQCurveKeyboard.cpp -- how EQCurveComponent is reached from the keyboard and a screen reader: the
// keys, the spoken value of the selected band, and the focus behaviour. EQCurveComponent is declared
// (and its mouse and paint code lives) in EQCurveComponent.h.
//
// The keys are the rebindable EQ actions (Settings, EQ), resolved through the ShortcutManager exactly like the drum
// grid's: with no manager the shipped defaults below, with a manager that does not know an id no key at all. They are
// tried before the fixed keys, and before the Cmd/Ctrl bail-out, so a rebound action may carry Cmd (Cmd+D by default)
// and reaches the curve before the app's own duplicate command sees it. Shift on a stepping action is its fine step.
// Fixed: 1-4, Page Up/Down (width), Return (on or off), Delete (remove the selection and select the next band), Cmd+A
// (select every band) and Escape (end this curve's solo). The arrows that move a band are held keys
// (EQCurveKeyGlide.cpp).

#include "EQCurveComponent.h"

#include "EQBandKinds.h"
#include "EQCurveHoldState.h"
#include "ModuleViewAccessibility.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/PointEditing/PointEditorKeys.h"
#include <utility>

namespace {
juce::KeyPress defaultKey(const juce::String& id) {
    const auto none = juce::ModifierKeys::noModifiers;
    const auto alt = juce::ModifierKeys::altModifier;
    if (id == "eqSelectPrev")
        return {juce::KeyPress::leftKey, none, 0};
    if (id == "eqSelectNext")
        return {juce::KeyPress::rightKey, none, 0};
    if (id == "eqMoveLeft")
        return {juce::KeyPress::leftKey, alt, 0};
    if (id == "eqMoveRight")
        return {juce::KeyPress::rightKey, alt, 0};
    if (id == "eqMoveUp")
        return {juce::KeyPress::upKey, alt, 0};
    if (id == "eqMoveDown")
        return {juce::KeyPress::downKey, alt, 0};
    if (id == "eqWidthUp")
        return {juce::KeyPress::upKey, none, 0};
    if (id == "eqWidthDown")
        return {juce::KeyPress::downKey, none, 0};
    if (id == "eqSolo")
        return {'s', none, 0};
    if (id == "eqTypeMenu")
        return {'t', none, 0};
    if (id == "eqAddBand")
        return {'+', none, 0};
    if (id == "eqRemoveBand")
        return {juce::KeyPress::backspaceKey, none, 0};
    if (id == "eqDuplicateBand")
        return {'d', juce::ModifierKeys::commandModifier, 0};
    return {};
}

// A bare plus binding is also how the key arrives from a US keyboard (Shift and =, or the plus itself with Shift held)
// and from the number pad.
bool plusKeyArrives(const juce::KeyPress& binding, const juce::KeyPress& key) {
    if (binding.getKeyCode() != '+' || binding.getModifiers() != juce::ModifierKeys())
        return false;
    if (key.getKeyCode() == juce::KeyPress::numberPadAdd)
        return key.getModifiers() == juce::ModifierKeys();
    const bool shiftOnly = key.getModifiers() == juce::ModifierKeys(juce::ModifierKeys::shiftModifier);
    return shiftOnly && (key.getKeyCode() == '+' || key.getKeyCode() == '=');
}

// A semitone, in octaves, and the Shift (fine) fraction every keyboard step shrinks to (EQCurveKeyGlide.cpp holds the
// same steps for a held key).
constexpr float kSemitoneOctaves = 1.0f / 12.0f;
constexpr float kFineFactor = 0.25f;
constexpr float kGainStepDb = 1.0f;
constexpr float kQStepOctaves = 0.25f;
} // namespace

juce::String EQCurveComponent::keyHint(const ShortcutManager* manager, const char* actionId) {
    return shortcutHintFor(manager, actionId, defaultKey(actionId));
}

// A manager that has never heard of an id answers with an invalid binding, which means "no key"; the default is only
// for a curve with no manager at all.
bool EQCurveComponent::matchesAction(const juce::KeyPress& key, const char* actionId) const {
    const auto binding = shortcuts_ == nullptr ? defaultKey(actionId) : shortcuts_->getBinding(actionId);
    return binding.isValid() && (ShortcutManager::keyPressMatches(binding, key) || plusKeyArrives(binding, key));
}

// Like matchesAction, but Shift on top of the binding also counts: the same step, made `fine`.
bool EQCurveComponent::matchesStepAction(const juce::KeyPress& key, const char* actionId, bool& fine) const {
    fine = false;
    if (matchesAction(key, actionId))
        return true;
    if (!key.getModifiers().isShiftDown())
        return false;
    const juce::KeyPress unshifted(key.getKeyCode(), key.getModifiers().withoutFlags(juce::ModifierKeys::shiftModifier),
                                   key.getTextCharacter());
    fine = matchesAction(unshifted, actionId);
    return fine;
}

juce::String EQCurveComponent::getTooltip() {
    juce::StringArray parts;
    const auto add = [&](const juce::String& keys, const char* what) {
        if (keys.isNotEmpty())
            parts.add(keys + " " + what);
    };
    const auto pair = [this](const char* a, const char* b) {
        const auto first = keyHint(shortcuts_, a), second = keyHint(shortcuts_, b);
        return first.isNotEmpty() && second.isNotEmpty() ? first + " / " + second : juce::String();
    };
    add(pair("eqSelectPrev", "eqSelectNext"), "pick a band (with Shift, extend)");
    add(pair("eqWidthUp", "eqWidthDown"), "its width");
    add(pair("eqMoveLeft", "eqMoveRight"), "its frequency");
    add(pair("eqMoveUp", "eqMoveDown"), "its gain");
    add(keyHint(shortcuts_, "eqSolo"), "solos it");
    add(keyHint(shortcuts_, "eqTypeMenu"), "opens its type");
    add(keyHint(shortcuts_, "eqAddBand"), "adds a band");
    add(keyHint(shortcuts_, "eqDuplicateBand"), "duplicates it");
    add(keyHint(shortcuts_, "eqRemoveBand"), "removes it");
    parts.add("Return switches it on or off");
    return "EQ curve: " + parts.joinIntoString(", ") +
           ". Mouse: double-click to add or remove a point, drag to move, drag a box to select several, scroll for Q, "
           "right-click a point for its type, Option-click to solo it";
}

void EQCurveComponent::selectBandRelative(int delta) {
    const int last = ParametricEQModule::kMaxBands - 1;
    setSelectedBand(selectedBand < 0 ? 0 : juce::jlimit(0, last, selectedBand + delta));
    repaint();
}

bool EQCurveComponent::toggleSelectedBand() {
    if (selectedBand < 0)
        return false;
    const int band = selectedBand;
    if (eqModule.isBandEnabled(band)) {
        removeBand(band);
    } else {
        beginGesture();
        eqModule.setBandEnabled(band, true);
        endGesture();
    }
    setSelectedBand(band);
    refreshFromModule();
    return true;
}

juce::String EQCurveComponent::getAccessibilityValueText() const {
    if (selectedBand < 0)
        return synth::ui::describeEqCurveSummary(eqModule.getEnabledBandCount(), ParametricEQModule::kMaxBands);
    const auto& band = lastBands[(size_t)selectedBand];
    const auto spoken = synth::ui::describeEqBand(selectedBand, synth::ui::eqBandTypeName(band.type), band.enabled,
                                                  band.freqHz, band.gainDb, band.q);
    return hold_->selection.size() > 1 ? juce::String(hold_->selection.size()) + " bands selected, " + spoken : spoken;
}

void EQCurveComponent::refreshAccessibilityValue() {
    const auto text = getAccessibilityValueText();
    if (text == announcedValueText)
        return;
    announcedValueText = text;
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

std::unique_ptr<juce::AccessibilityHandler> EQCurveComponent::createAccessibilityHandler() {
    return synth::ui::makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

void EQCurveComponent::focusGained(FocusChangeType cause) {
    // Tabbing in lands on a band, so the screen reader has something to read straight away.
    if (cause != focusChangedByMouseClick && selectedBand < 0)
        setSelectedBand(0);
    repaint();
}

// The rebindable actions. Every one of them consumes its key even when there is nothing to act on (no band selected,
// all 24 on): a key this curve owns must never fall through to the canvas, where Backspace or Cmd+D would act on a
// module.
bool EQCurveComponent::handleActionKey(const juce::KeyPress& key) {
    bool fine = false;
    if (matchesAction(key, "eqDuplicateBand")) {
        duplicateSelectedBand();
        return true;
    }
    // The Alt chords first: Shift+Alt+Up is a fine gain step, never a width step.
    struct Move {
        const char* id;
        float octaves;
        float db;
    };
    for (const auto& move : {Move{"eqMoveLeft", -kSemitoneOctaves, 0.0f}, Move{"eqMoveRight", kSemitoneOctaves, 0.0f},
                             Move{"eqMoveUp", 0.0f, kGainStepDb}, Move{"eqMoveDown", 0.0f, -kGainStepDb}})
        if (matchesStepAction(key, move.id, fine)) {
            const float scale = fine ? kFineFactor : 1.0f;
            return pressMove(key.getKeyCode(), move.octaves * scale, move.db * scale, fine);
        }
    // Up widens (a lower Q), Down narrows, like Page Up/Down's opposite.
    for (const auto& width : {std::pair<const char*, float>{"eqWidthUp", -kQStepOctaves},
                              std::pair<const char*, float>{"eqWidthDown", kQStepOctaves}})
        if (matchesStepAction(key, width.first, fine)) {
            if (ensureSelectedBand())
                nudgeBandQ(selectedBand, width.second * (fine ? kFineFactor : 1.0f));
            return true;
        }
    // Shift+Left/Right grows the selection one band, like every other point editor.
    for (const auto& pick :
         {std::pair<const char*, int>{"eqSelectPrev", -1}, std::pair<const char*, int>{"eqSelectNext", 1}})
        if (matchesStepAction(key, pick.first, fine)) {
            if (fine)
                extendSelectionRelative(pick.second);
            else
                selectBandRelative(pick.second);
            return true;
        }
    if (matchesAction(key, "eqSolo")) {
        toggleSoloSelectedBand();
        return true;
    }
    if (matchesAction(key, "eqTypeMenu")) {
        if (selectedBand >= 0)
            showTypeMenuAtDot(selectedBand);
        return true;
    }
    if (matchesAction(key, "eqAddBand")) {
        addBandInLargestGap();
        return true;
    }
    if (matchesAction(key, "eqRemoveBand")) {
        removeSelectedBands(false); // Backspace keeps the selection on the band, so Return can bring it back
        return true;
    }
    return false;
}

bool EQCurveComponent::handleFixedKey(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (key == juce::KeyPress::escapeKey) {
        if (!ownsSolo_)
            return false; // the window or menu closes as it always did
        endSolo();
        return true;
    }
    if (mods.isCommandDown() || mods.isCtrlDown())
        return false;
    if (code >= '1' && code < '1' + ParametricEQModule::kOriginalBands) {
        selectBand(code - '1');
        return true;
    }
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) {
        const float fine = mods.isShiftDown() ? kFineFactor : 1.0f;
        if (ensureSelectedBand())
            nudgeBandQ(selectedBand, (code == juce::KeyPress::pageUpKey ? kQStepOctaves : -kQStepOctaves) * fine);
        return true;
    }
    if (code == juce::KeyPress::deleteKey) {
        removeSelectedBands(true); // Delete moves the selection to the next band
        return true;
    }
    if (code == juce::KeyPress::returnKey) {
        toggleSelectedBand();
        return true;
    }
    return false;
}

bool EQCurveComponent::keyPressed(const juce::KeyPress& key) {
    if (!getWantsKeyboardFocus())
        return false;
    const int code = key.getKeyCode();
    if (hold_->hold.active() && synth::ui::KeyHold::isArrow(code) && hold_->hold.anyArrowDown())
        return true; // an arrow joining a running hold (or its auto-repeat): the hold reads it each frame
    if (synth::ui::pointKeyAction(key).kind == synth::ui::PointKeyAction::Kind::selectAll) {
        selectAllBands();
        return true;
    }
    return handleActionKey(key) || handleFixedKey(key);
}
