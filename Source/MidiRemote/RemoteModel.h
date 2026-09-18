#pragma once

// Headless MIDI Remote data model (docs/control/midi-remote.md#data-model). Lives in Core: no
// juce::ApplicationProperties / juce::PropertiesFile include here or anywhere this header is
// used from, per Source/CLAUDE.md's Core-layering rule — the file store that persists a
// ControllerProfile to disk is app-layer (Source/MidiRemote/ControllerProfileStore.h), injected
// from MainComponent, never owned here. This header only defines values and declares their JSON
// shape; the JSON is implemented in RemoteModelJson.cpp.

#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

// -- Enums (docs/control/midi-remote.md#data-model) --------------------------------------------------------------
// Every enum here serialises as its doc-exact camelCase string (see RemoteModelJson.cpp); an
// unrecognised string on load is a hard failure in fromVar, never a silent default.

enum class ControlKind { knob, fader, button, pad, encoder, wheel };

enum class MessageType { cc, note, pitchBend, channelPressure, programChange };

// abs14 (14-bit MSB/LSB pairs) is a v2 extension (FRO140) — not modelled here.
enum class Encoding { abs7, relTwos, relBinOffset, relSignMag };

enum class ButtonMode { momentary, toggle };

// "default" is a C++ keyword, so the "use the Preferences default takeover" enumerator is named
// useDefault instead (docs/control/midi-remote.md#takeover / docs/control/midi-remote.md#data-model).
enum class Takeover { jump, pickup, scale, useDefault };

// -- MessageSpec ----------------------------------------------------------------------------------
/** The engine's lookup key for a hardware message (docs/control/midi-remote.md#data-model): (type, channel,
 *  number). `channel` == 0 means "any channel", else 1..16. `number` is the cc/note number and is
 *  ignored for pitchBend/channelPressure. Two controls on one ControllerProfile may not share a
 *  MessageSpec — see ControllerProfile::fromVar. */
struct MessageSpec {
    MessageType type = MessageType::cc;
    int channel = 0; // 0 = any, else 1..16
    int number = 0;  // 0..127

    bool operator==(const MessageSpec& other) const {
        return type == other.type && channel == other.channel && number == other.number;
    }
    bool operator!=(const MessageSpec& other) const { return !(*this == other); }

    juce::var toVar() const;
    /** All-or-nothing: a malformed field leaves `out` untouched and returns false. */
    static bool fromVar(const juce::var& v, MessageSpec& out);
};

// -- Control ---------------------------------------------------------------------------------------
/** One physical control on a ControllerProfile's detected surface (docs/control/midi-remote.md#data-model). */
struct Control {
    juce::String id;
    juce::String name;
    ControlKind kind = ControlKind::knob;
    MessageSpec message;
    Encoding encoding = Encoding::abs7;
    ButtonMode buttonMode = ButtonMode::momentary; // buttons/pads only
    struct {
        int col = 0;
        int row = 0;
    } layout;

    juce::var toVar() const;
    /** All-or-nothing: a malformed field (including an empty id) leaves `out` untouched and
     *  returns false. */
    static bool fromVar(const juce::var& v, Control& out);
};

// -- Target ----------------------------------------------------------------------------------------
/** An Assignment's destination: exactly one of a graph parameter or a ShortcutManager-registered
 *  action (docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project,
 *  docs/control/midi-remote.md#action-targets, docs/control/midi-remote.md#data-model). Modelled
 *  as a tagged union (rather than two std::optional payloads) so fromVar can reject a JSON object
 *  carrying both "parameter" and "action", or neither, as a single well-defined check. */
struct Target {
    enum class Kind { parameter, action };

    struct Parameter {
        juce::String nodeUuid;
        juce::String paramId;
        int paramIndexHint = -1; // same fallback shape as AutomationLane::paramIndexHint; -1 = none
    };
    struct Action {
        juce::String actionId; // ShortcutManager / juce::CommandID-backed action id
    };

    Kind kind = Kind::parameter;
    Parameter parameter;
    Action action;

    bool isParameter() const noexcept { return kind == Kind::parameter; }
    bool isAction() const noexcept { return kind == Kind::action; }

    // Only the payload matching `kind` is written — see fromVar for the both/neither rejection.
    juce::var toVar() const;
    /** All-or-nothing: rejects (returns false, leaves `out` untouched) if the JSON object carries
     *  BOTH "parameter" and "action", or NEITHER. */
    static bool fromVar(const juce::var& v, Target& out);
};

// -- Assignment -------------------------------------------------------------------------------------
/** One mapping from a control's message to a Target. Carries a DENORMALISED copy of the
 *  control's message spec (docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project's
 * "consequence") so it round-trips and stays meaningful even when the ControllerProfile it references isn't present on
 * this machine — there is no cross-reference/lookup against a live profile at parse time. */
struct Assignment {
    juce::String id;

    // The GLOBAL control this assignment maps, by reference only — may not resolve on this
    // machine (an orphan controller, docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project).
    struct {
        juce::String profileId;
        juce::String controlId;
    } control;

    // Denormalised copy of the control's message spec at assignment time.
    MessageSpec spec;
    Encoding specEncoding = Encoding::abs7;
    ButtonMode specButtonMode = ButtonMode::momentary;
    juce::String specControlName;

    Target target;
    Takeover takeover = Takeover::useDefault;

    struct {
        double min = 0.0;
        double max = 1.0;
    } range; // normalised; min > max inverts (docs/control/midi-remote.md#data-model)

    bool enabled = true;

    juce::var toVar() const;
    /** All-or-nothing: a malformed field (including an invalid Target, per Target::fromVar's own
     *  rule) leaves `out` untouched and returns false. */
    static bool fromVar(const juce::var& v, Assignment& out);
};

// -- ControllerProfile --------------------------------------------------------------------------------
/** GLOBAL — one per physical controller (docs/control/midi-remote.md#data-model). Persisted by the app-layer
 *  ControllerProfileStore as one JSON file per profile (Source/MidiRemote/
 *  ControllerProfileStore.h); this struct only knows its own JSON shape. */
struct ControllerProfile {
    juce::String id;
    juce::String name;

    struct Input {
        juce::String identifier; // juce::MidiDeviceInfo::identifier — matches first
        juce::String name;       // fallback match
    };
    Input input;

    // Reserved for a v2 feedback extension (FRO139); never read in v1. A nullable
    // struct: hasOutput == false means "no output device set" and `output` itself is inert.
    bool hasOutput = false;
    Input output;

    // docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph's
    // "also pass mapped messages to the patch" toggle, default off
    bool passMapped = false;

    std::vector<Control> controls;
    std::vector<Assignment> actions; // GLOBAL assignments: target.kind == action only

    int version = 1;

    juce::var toVar() const;

    /** All-or-nothing (mirrors TimelineDoc::fromVar): a malformed field, an unrecognised enum
     *  string, a missing/wrong "version" (must equal exactly 1), a Target with both/neither of
     *  parameter+action, or two controls sharing a MessageSpec all reject the WHOLE load and
     *  leave `this` completely untouched. */
    bool fromVar(const juce::var& state);
};

// -- MidiRemoteProjectDoc ------------------------------------------------------------------------------
/** The project's reserved `"midiRemote"` top-level key value type (docs/control/midi-remote.md#data-model,
 * docs/control/midi-remote.md#persistence-and-the-trust-boundary) — mirrors synth::TimelineDoc's / synth::MacroSet's
 * role for ProjectBundle: a plain, headless, serialisable value ProjectBundle reads/writes as a whole, with no engine
 * or file-store dependency of its own. */
class MidiRemoteProjectDoc {
public:
    int version = 1;
    std::vector<Assignment> assignments; // target.kind == parameter only

    struct ControllerRef {
        juce::String profileId;
        juce::String name;
    };
    std::vector<ControllerRef>
        controllers; // orphan-controller display,
                     // docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project

    juce::var toVar() const;

    /** All-or-nothing, same contract as ControllerProfile::fromVar / TimelineDoc::fromVar: a
     *  malformed field, an unrecognised enum string, or a missing/wrong "version" (must equal
     *  exactly 1) rejects the WHOLE load and leaves `this` completely untouched. */
    bool fromVar(const juce::var& state);
};

} // namespace synth
