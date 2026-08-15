#pragma once

#include "../Transport/TransportService.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {
class Metronome; // Forward declaration (Source/Transport/Metronome.h)
} // namespace synth

// TimelineTransportBar — TL5-5: play/stop/record/loop + BPM/time-signature editors + the
// bar:beat readout, hosted at the LEFT of the timeline panel's transport-bar strip (the snap
// combo stays at the right — see TimelinePanelComponent::resized()).
//
// Owns no transport state of its own: a non-owning synth::TransportService* is the only thing it
// ever talks to for play/stop/loop/bpm/time-sig commands, read fresh at the moment of each click
// or edit rather than cached — a loop toggle re-posts whatever [start, end) the transport is
// CURRENTLY reporting (which is TransportService's own [0, 4) default the very first time, and
// preserves anything set since), and an invalid time-signature edit reverts the label to the
// transport's current (not a remembered) value. The panel's EXISTING 10 Hz poll
// (TimelinePanelComponent::updateFromTransport, itself driven by MainComponent's timer — see
// docs/layout.md §11) is the only thing that ever calls this bar's own updateFromTransport(): no
// timer here, no per-frame repaint. It resyncs the play/loop button VISUALS and the BPM/time-sig
// LABEL TEXT from the snapshot (so a Space-bar play triggered elsewhere is reflected within one
// tick), and refreshes the bar:beat readout, string-diffed so an unchanged tick repaints nothing.
//
// Recording is the one control this bar is NOT authoritative over: starting a take requires an
// armed MIDI track, which only MainComponent can see (it owns the TimelineDoc). onRecordToggled
// reports the user's INTENT; the owner decides whether it actually happens and calls
// setRecordingState() with the real outcome — see docs/architecture.md's MidiRecorder wiring entry
// and MainComponent's SYNTH_ENABLE_TIMELINE wiring block.
//
// No SVG assets: the four transport glyphs (play/stop, record, loop, metronome) are drawn as plain
// juce::Path/juce::Rectangle shapes in GlyphButton::paintButton — see the CLAUDE.md invariant on
// themes never swapping typefaces; this is the equivalent "draw it, don't asset it" rule for a
// handful of one-off shapes that would otherwise need new icon assets for a single caller.
//
// Headless-safe: paint() dynamic_cast<AppLookAndFeel*>s and falls back to plain colours, same
// pattern as every other timeline-panel component.
//
// TL5-6: metronome + count-in. `metronomeButton_` is a direct on/off toggle (unlike record: there is
// no owner-side veto, so the click itself flips both the button's visual state and the metronome —
// no onMetronomeToggled intent/outcome split is needed). `countInCombo_` ("Off"/"1 bar"/"2 bars") is
// read by MainComponent's record flow at the moment Record is clicked, never cached elsewhere. Both
// controls persist themselves (`setApplicationProperties`, same restore/apply-on-change idiom
// TimelinePanelComponent's snap combo uses) rather than routing through the panel, since the panel
// has no other reason to know either setting exists.
namespace synth::ui {

class TimelineTransportBar : public juce::Component {
public:
    TimelineTransportBar();
    ~TimelineTransportBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Non-owning; may be null (tests, or before the panel finishes wiring) — every command below
    // is then a no-op, matching TimelineRulerComponent/TimelinePlayheadOverlay's own null contract.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    // THE drive seam, called from TimelinePanelComponent::updateFromTransport (which is itself
    // driven by MainComponent's existing 10 Hz timer) — never any faster. See the class comment.
    void updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot);

    // The record button's click reports intent only — see the class comment. `true` means "please
    // start a take", which the owner may immediately refuse (no armed track); `false` always means
    // "stop and commit".
    std::function<void(bool)> onRecordToggled;

    // THE authoritative setter for the record button's visual state — the ONLY thing that ever
    // calls setToggleState on it. Idempotent: juce::Button's own setToggleState is already a no-op
    // (repaint-wise) when the state isn't actually changing.
    void setRecordingState(bool recording) noexcept;
    bool isRecordingForTest() const noexcept { return recordButton_.getToggleState(); }

    // TL5-6: metronome + count-in. Non-owning; may be null (tests, or before MainComponent finishes
    // wiring) — the button click is then inert, matching setTransport's null contract. Whichever of
    // setMetronome()/setApplicationProperties() runs SECOND is what makes the persisted enabled
    // state real: each applies the currently-known enabled value to `metronome` if the other has
    // already run, so callers may wire them in either order.
    void setMetronome(synth::Metronome* metronome) noexcept;
    synth::Metronome* getMetronome() const noexcept { return metronome_; }

    // Restores ("timelineMetronomeEnabled" bool, default off; "timelineCountInBars" int 0-2, default
    // 0) and re-persists on every change — the same restore-then-persist-on-change idiom
    // TimelinePanelComponent::setApplicationProperties uses for its snap combo.
    void setApplicationProperties(juce::ApplicationProperties* props);

    // 0 = off, 1 = one bar, 2 = two bars. Read by MainComponent's record flow at the moment Record
    // is clicked (TL5-6) — never cached by the caller.
    int getCountInBars() const noexcept { return countInBars_; }

    // ---- test accessors ----
    juce::Button& getMetronomeButton() noexcept { return metronomeButton_; }
    juce::ComboBox& getCountInCombo() noexcept { return countInCombo_; }

    // "BAR.BEAT.TICKS" — 1-based bar (zero-padded to 3 digits), 1-based beat (unpadded), and ticks
    // = 1/960 of a beat (zero-padded to 3 digits). Pure — no JUCE GUI dependency, so it is testable
    // headlessly with no component at all, same contract as StatusBarComponent's static helpers.
    // See Tests/TimelineTransportBarTests.cpp's FormatBarBeatTable for the pinned strings.
    static juce::String formatBarBeat(double ppq, int tsNumerator, int tsDenominator);

    // ---- test accessors ----
    juce::Button& getPlayStopButton() noexcept { return playStopButton_; }
    juce::Button& getRecordButton() noexcept { return recordButton_; }
    juce::Button& getLoopButton() noexcept { return loopButton_; }
    juce::Label& getBpmLabel() noexcept { return bpmLabel_; }
    juce::Label& getTimeSigLabel() noexcept { return timeSigLabel_; }
    juce::String getReadoutTextForTest() const noexcept { return lastReadoutText_; }
    /** How many times the readout's cached string actually changed (and so requested a repaint) —
     *  the test hook that proves the string-diff gate, the same counting idiom
     *  TimelinePanelComponent::getTransportUpdateCountForTest() uses. */
    int getReadoutRepaintCountForTest() const noexcept { return readoutRepaintCount_; }

private:
    // A small juce::Button subclass that draws one of the transport glyphs as a plain path — see
    // the class comment. getToggleState() selects which visual each glyph shows: play vs stop for
    // PlayStop, outline vs filled-red for Record, dim vs lit-accent for Loop and Metronome.
    class GlyphButton : public juce::Button {
    public:
        enum class Glyph { PlayStop, Record, Loop, Metronome };
        GlyphButton(const juce::String& name, Glyph glyph)
            : juce::Button(name)
            , glyph_(glyph) {}
        void paintButton(juce::Graphics& g, bool shouldDrawHighlighted, bool shouldDrawDown) override;

    private:
        Glyph glyph_;
    };

    // A juce::Label that turns a vertical drag into a live BPM change: ±1.0 BPM per 4 px, or
    // ±0.1 BPM per 4 px with Cmd held (fine). Double-click-to-edit (inherited, unmodified) and
    // drag-to-scrub are independent gestures — JUCE dispatches mouseDoubleClick separately from
    // mouseDown/mouseDrag/mouseUp, so overriding the latter three here does not disturb Label's
    // own editDoubleClick handling.
    class BpmDragLabel : public juce::Label {
    public:
        explicit BpmDragLabel(TimelineTransportBar& owner) noexcept
            : owner_(owner) {}
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;

    private:
        TimelineTransportBar& owner_;
        float dragAnchorY_ = 0.0f;
        double dragAnchorBpm_ = 120.0;
    };

    double currentBpmForDrag() const noexcept;
    void applyDraggedBpm(double anchorBpm, float deltaY, bool fine);
    void refreshReadout(const synth::TransportService::PositionSnapshot& snapshot);
    static juce::String formatBpm(double bpm);
    static juce::String formatTimeSig(int numerator, int denominator);

    synth::TransportService* transport_ = nullptr;

    GlyphButton playStopButton_{"timelineTransportPlayStop", GlyphButton::Glyph::PlayStop};
    GlyphButton recordButton_{"timelineTransportRecord", GlyphButton::Glyph::Record};
    GlyphButton loopButton_{"timelineTransportLoop", GlyphButton::Glyph::Loop};
    GlyphButton metronomeButton_{"timelineTransportMetronome", GlyphButton::Glyph::Metronome};
    juce::ComboBox countInCombo_;
    BpmDragLabel bpmLabel_{*this};
    juce::Label timeSigLabel_;

    juce::String lastReadoutText_;
    int readoutRepaintCount_ = 0;
    juce::Rectangle<int> readoutBounds_;

    // TL5-6. See setMetronome()/setApplicationProperties(): `pendingMetronomeEnabled_` is the
    // last-known-enabled value regardless of which of the two setters supplied it, applied to
    // `metronome_` whichever runs second.
    synth::Metronome* metronome_ = nullptr;
    bool pendingMetronomeEnabled_ = false;
    int countInBars_ = 0;
    juce::ApplicationProperties* appProperties_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineTransportBar)
};

} // namespace synth::ui
