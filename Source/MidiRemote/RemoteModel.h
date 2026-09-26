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

// nrpn (FRO140): a 14-bit parameter address (CC 99/98) whose value arrives as data-entry CCs 6/38.
// MessageSpec::number is the ADDRESS (0..16383), so an NRPN key can never collide with CC n.
enum class MessageType { cc, note, pitchBend, channelPressure, programChange, nrpn };

// abs14 / abs14LsbFirst (FRO140) are 14-bit absolute values carried by TWO messages: CC n (MSB) with
// CC n+32 (LSB) for a cc control (n 0..31), or data-entry CC 6 / CC 38 for an nrpn control. The
// value is committed when the SECOND half arrives -- abs14 waits for the LSB, abs14LsbFirst for the
// MSB -- and the other half is the last one seen. On an nrpn control abs7 means "CC 6 only".
// Appended after the relative encodings: the inspector's combo ids are declaration order.
enum class Encoding { abs7, relTwos, relBinOffset, relSignMag, abs14, abs14LsbFirst };

/** MSB CC n pairs with LSB CC n + kPairedLsbOffset (MIDI 1.0 CC 0..31 / 32..63). */
inline constexpr int kPairedLsbOffset = 32;
/** NRPN address CCs and the data-entry CCs that carry its value. */
inline constexpr int kNrpnAddressMsbCc = 99;
inline constexpr int kNrpnAddressLsbCc = 98;
inline constexpr int kRpnAddressMsbCc = 101; // RPN select: cancels an armed NRPN address
inline constexpr int kRpnAddressLsbCc = 100;
inline constexpr int kDataEntryMsbCc = 6;
inline constexpr int kDataEntryLsbCc = 38;
inline constexpr int kMaxNrpnAddress = 16383;

inline bool isPairedEncoding(Encoding e) noexcept { return e == Encoding::abs14 || e == Encoding::abs14LsbFirst; }

enum class ButtonMode { momentary, toggle };

// "default" is a C++ keyword, so the "use the Preferences default takeover" enumerator is named
// useDefault instead (docs/control/midi-remote.md#takeover / docs/control/midi-remote.md#data-model).
enum class Takeover { jump, pickup, scale, useDefault };

// FRO253 (docs/control/midi-remote.md#node-command-targets): what a Target::NodeCommand asks the
// app-layer invoker to do to one graph node. toggleSolo is the only member today (the mixer
// column's Solo button, which is engine state -- ChannelStripModule::soloed_ -- not a
// juce::RangedAudioParameter, so it has no Target::Parameter to point at); a second node command
// extends this enum rather than growing Target with a fourth kind.
enum class NodeCommandKind { toggleSolo };

// FRO236 (docs/control/midi-remote.md#continuous-targets): what a Target::Continuous drives. Unlike
// a parameter/action/nodeCommand target these are never resolved against a graph node by uuid --
// bpm and playhead reach the app layer's transport through RemoteActionInvoker's continuous
// methods, and masterVolume resolves through a separate injected ContinuousParameterLookup (Core
// must not include MasterModule.h) straight to the SAME juce::AudioProcessorParameter* the mixer's
// own master fader binds, so it reuses applyToParameter/RemoteEngineFeedback.cpp verbatim.
enum class ContinuousTargetKind { bpm, playhead, masterVolume };

// FRO142 (docs/control/midi-remote.md#pages): what a Target::Page button asks the engine to do to
// the ACTIVE PAGE of the profile that owns the control that fired it (never routed through
// ShortcutManager / ActionCommandLookup -- this is engine-internal, unlike Kind::action). `go`
// is the only command that reads Target::Page::page; next/previous wrap around the profile's
// effective page count (ControllerProfile::pageCount, widened by the highest `page` any project
// assignment on that profile uses).
enum class PageCommand { next, previous, go };

// -- MessageSpec ----------------------------------------------------------------------------------
/** The engine's lookup key for a hardware message (docs/control/midi-remote.md#data-model): (type, channel,
 *  number). `channel` == 0 means "any channel", else 1..16. `number` is the cc/note number and is
 *  ignored for pitchBend/channelPressure. Two controls on one ControllerProfile may not share a
 *  MessageSpec — see ControllerProfile::fromVar. */
struct MessageSpec {
    MessageType type = MessageType::cc;
    int channel = 0; // 0 = any, else 1..16
    int number = 0;  // 0..127; nrpn: the 14-bit address 0..16383

    bool operator==(const MessageSpec& other) const {
        return type == other.type && channel == other.channel && number == other.number;
    }
    bool operator!=(const MessageSpec& other) const { return !(*this == other); }

    /** Highest legal `number` for `type` (nrpn: a 14-bit address; everything else 7-bit). */
    static constexpr int maxNumber(MessageType t) noexcept { return t == MessageType::nrpn ? kMaxNrpnAddress : 127; }

    juce::var toVar() const;
    /** All-or-nothing: a malformed field leaves `out` untouched and returns false. */
    static bool fromVar(const juce::var& v, MessageSpec& out);
};

/** Whether `encoding` may describe a control on `spec`: a paired encoding needs a cc in 0..31 (its
 *  partner is number+32) or an nrpn; an nrpn carries only abs7 (CC 6 alone) or a paired encoding.
 *  Every other combination stays as permissive as it always was. */
inline bool encodingValidForSpec(const MessageSpec& spec, Encoding encoding) noexcept {
    if (isPairedEncoding(encoding))
        return spec.type == MessageType::nrpn || (spec.type == MessageType::cc && spec.number + kPairedLsbOffset <= 63);
    if (spec.type == MessageType::nrpn)
        return encoding == Encoding::abs7;
    return true;
}

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

    // FRO141 (docs/control/midi-remote.md#focus-bank): membership in the controller setup's "focus
    // bank" -- the set of controls that follow the canvas selection instead of holding a fixed
    // mapping. This is the only thing about the focus bank that persists; the CURRENT binding
    // (which control drives which parameter right now) never does -- see RemoteEngine::
    // setTransientAssignments(). Written to JSON only when true, so a pre-FRO141 profile round-trips
    // byte-identical.
    bool focusBank = false;

    juce::var toVar() const;
    /** All-or-nothing: a malformed field (including an empty id) leaves `out` untouched and
     *  returns false. */
    static bool fromVar(const juce::var& v, Control& out);
};

// -- Target ----------------------------------------------------------------------------------------
/** An Assignment's destination: exactly one of a graph parameter, a ShortcutManager-registered
 *  action, a node command, or a continuous target
 * (docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project,
 *  docs/control/midi-remote.md#action-targets, docs/control/midi-remote.md#node-command-targets,
 *  docs/control/midi-remote.md#continuous-targets, docs/control/midi-remote.md#data-model). Modelled
 *  as a tagged union (rather than four std::optional payloads) so fromVar can reject a JSON object
 *  carrying more than one of "parameter"/"action"/"nodeCommand"/"continuous", or none, as a single
 *  well-defined check. */
struct Target {
    enum class Kind { parameter, action, nodeCommand, continuous, page };

    struct Parameter {
        juce::String nodeUuid;
        juce::String paramId;
        int paramIndexHint = -1; // same fallback shape as AutomationLane::paramIndexHint; -1 = none
    };
    struct Action {
        juce::String actionId; // ShortcutManager / juce::CommandID-backed action id
    };
    // FRO253: a graph node this doesn't resolve to a parameter for -- see NodeCommandKind's own
    // comment. Resolved by nodeUuid, exactly like Parameter::nodeUuid.
    struct NodeCommand {
        juce::String nodeUuid;
        NodeCommandKind command = NodeCommandKind::toggleSolo;
    };
    // FRO236: no nodeUuid -- see ContinuousTargetKind's own comment on how each kind resolves.
    struct Continuous {
        ContinuousTargetKind kind = ContinuousTargetKind::bpm;
    };
    // FRO142 (docs/control/midi-remote.md#pages): a button-like, engine-internal target -- see
    // PageCommand's own comment. `page` is 1-based and only meaningful for command == go.
    struct Page {
        PageCommand command = PageCommand::next;
        int page = 1;
    };

    Kind kind = Kind::parameter;
    Parameter parameter;
    Action action;
    NodeCommand nodeCommand;
    Continuous continuous;
    Page page;

    bool isParameter() const noexcept { return kind == Kind::parameter; }
    bool isAction() const noexcept { return kind == Kind::action; }
    bool isNodeCommand() const noexcept { return kind == Kind::nodeCommand; }
    bool isContinuous() const noexcept { return kind == Kind::continuous; }
    bool isPage() const noexcept { return kind == Kind::page; }

    // Only the payload matching `kind` is written — see fromVar for the "exactly one" rejection.
    juce::var toVar() const;
    /** All-or-nothing: rejects (returns false, leaves `out` untouched) unless the JSON object
     *  carries EXACTLY ONE of "parameter"/"action"/"nodeCommand"/"continuous"/"page". */
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

    // FRO142 (docs/control/midi-remote.md#pages): 1-based. Meaningful for a PROJECT assignment
    // only (MidiRemoteProjectDoc::assignments) -- a GLOBAL profile action (ControllerProfile::actions)
    // ignores it entirely, since an action is active on every page. Missing on load == 1, so every
    // pre-FRO142 document round-trips unchanged.
    int page = 1;

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

    // FRO139 (docs/control/midi-remote.md#controller-feedback): RemoteEngine's drain sends every
    // mapped parameter's value back out to this device, picked per-controller from the panel's
    // Controllers-list right-click rather than the dead Audio-tab MIDI-output selector. A nullable
    // struct: hasOutput == false means "no output device set" and `output` itself is inert.
    bool hasOutput = false;
    Input output;

    // docs/control/midi-remote.md#are-mapped-messages-consumed-or-also-forwarded-to-the-graph's
    // "also pass mapped messages to the patch" toggle, default off
    bool passMapped = false;

    std::vector<Control> controls;
    // GLOBAL assignments: target.kind == action, continuous, or page only (FRO236: a continuous
    // target means the same thing in every project -- there is exactly one transport/master
    // volume -- exactly like an action, so it lives here rather than in a project's
    // MidiRemoteProjectDoc; FRO142: a page target is likewise engine-internal and active on every
    // page, so it belongs beside the other GLOBAL action kinds).
    std::vector<Assignment> actions;

    // FRO142 (docs/control/midi-remote.md#pages): how many mapping pages this controller has, 1..16.
    // A project assignment's own `page` (Assignment::page) may exceed this -- see
    // RemoteEngine::getEffectivePageCount -- so this is a floor, not a hard cap on what a project
    // can reference.
    int pageCount = 1;

    int version = 1;

    juce::var toVar() const;

    /** All-or-nothing (mirrors TimelineDoc::fromVar): a malformed field, an unrecognised enum
     *  string, a missing/wrong "version" (must equal exactly 1), a Target with anything other than
     *  exactly one of parameter/action/nodeCommand/continuous, or two controls sharing a
     *  MessageSpec all reject the WHOLE load and leave `this` completely untouched. */
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
    // FRO253/FRO236: a project assignment is never an action or a continuous target (both are
    // GLOBAL, ControllerProfile::actions only) -- parameter and nodeCommand targets both live here.
    std::vector<Assignment> assignments;

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
