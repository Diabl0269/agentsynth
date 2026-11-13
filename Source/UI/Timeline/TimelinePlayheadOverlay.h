#pragma once

#include "PlayheadWrapCompensation.h"
#include "TimelineViewState.h"
#include "Transport/TransportService.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// TimelinePlayheadOverlay — the vertical position line drawn over the timeline panel's
// ruler + lanes region.
//
// This is the SECOND (and, with the AI thinking spinner, the only other) documented exception to
// the app's no-unconditional-per-tick-repaint rule — see docs/layout/animation.md and the CLAUDE.md
// invariant. The exception is granted under a confinement contract, and every clause of it is
// enforced here:
//
//   1. PLAYING ONLY (or a clip preview, or a locate glide of at most 160 ms). A juce::VBlankAttachment, one frame per
//      display refresh, is created on the play transition and dropped on the stop/pause transition. It NEVER runs
//      while the transport is stopped and nothing else is moving, so an idle app repaints nothing at all.
//   2. STRIP ONLY. A frame never repaints the component: it repaints the OLD and NEW line strips (each kStripHalfWidth
//      px either side of the line), clipped to the bounds (a big move requests the two strips apart, not the span
//      between them). A frame where the rounded x did not move requests nothing.
//   3. EXPLICIT STOP. Stopping emits exactly ONE final strip repaint (so the line settles on the stop position) and
//      then goes silent.
//
// Transitions are NOT detected here. The owner polls the transport at a low rate (MainComponent's
// existing 10 Hz timer -> TimelinePanelComponent::updateFromTransport) and hands the snapshot in;
// this class owns the frame attachment's lifecycle from those calls alone (a locate glide that ends while stopped
// drops it from the frame itself). The frame re-reads the transport (through the stored, non-owning TransportService*)
// so the line moves smoothly between polls.
//
// Every snapshot first goes through PlayheadExtrapolator: the transport publishes its position once per audio block,
// the display runs faster, so the position is advanced by the time since it was published (at most a few ms) and the
// line moves by about the same amount on every frame. The wall clock is `nowMs()`, a protected virtual a test replaces.
//
// A jump the user asked for (arm it with armLocateGlide before the locate) glides the DRAWN line there over 160 ms
// (PlayheadLocateGlide); the audio jumps at once. Reduce Motion and a hidden panel land at once.
//
// The DRAWN position is deliberately not the transport position: it is offset backwards by the
// audio device's output latency (`ppq - outputLatencySeconds * bpm/60`, clamped >= 0), so the line
// lines up with what is being HEARD rather than with the block currently being rendered. At a loop
// wrap the line is carried on to the loop end first (PlayheadWrapCompensation).
//
// Headless-safe: paint() dynamic_cast<AppLookAndFeel*>s and falls back to a literal colour, and no
// path here needs a peer, a device or a message loop.
namespace synth::ui {

class PlayheadExtrapolator;
class PlayheadLocateGlide;

class TimelinePlayheadOverlay : public juce::Component {
public:
    // Line thickness, and the margin either side of it that a repaint strip must cover (the strip
    // has to include the pixels the ANTI-ALIASED line touched, not just its nominal width).
    static constexpr float kLineWidth = 2.0f;
    static constexpr int kStripHalfWidth = 2;

    // A sub-region of the overlay that maps beats to x through its OWN zoom/scroll — the piano
    // roll, whose grid starts right of a keys gutter and scrolls/zooms independently of the shared
    // TimelineViewState. While a client is attached AND reports itself active, the overlay treats
    // that region as not its own: it neither draws nor requests a repaint inside it, and instead
    // hands the DRAWN beat over on every refresh so the client can run the same strip-confined
    // discipline against its own mapping. The panel still has exactly ONE playhead frame source — this
    // one; a client never starts another.
    class LocalPlayheadClient {
    public:
        virtual ~LocalPlayheadClient() = default;
        virtual bool isLocalPlayheadActive() const = 0;
        virtual void setPlayheadBeat(double absoluteBeat) = 0;
    };

    explicit TimelinePlayheadOverlay(TimelineViewState& viewState);
    ~TimelinePlayheadOverlay() override;

    // Non-owning; may be null (tests, or before the panel is wired). Only the frames read it,
    // and only to re-read the position between the owner's low-rate polls.
    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    // Non-owning; may be null (the default — with no client the overlay owns its whole rect and
    // behaves exactly as it always did). See LocalPlayheadClient above.
    void setLocalPlayheadClient(LocalPlayheadClient* client) noexcept { localClient_ = client; }
    // The client's rect in THIS overlay's coordinates. The client is a sibling, so only the owner
    // (TimelinePanelComponent::resized) knows the offset — it sets this alongside the bounds.
    void setLocalPlayheadRegion(juce::Rectangle<int> region) noexcept { localRegion_ = region; }

    // Told the drawn beat on every refresh, before the line's x is read (every display frame while playing), so a
    // smooth follow can move the view first and the line then draws at its pinned x.
    std::function<void(double)> onDrawnBeat;

    // A drum machine clip loop runs with the transport stopped: the display frames run for it too. Start and stop only.
    void setClipPreviewActive(bool active);

    // The next locate this overlay sees glides the drawn line (see the class comment). Call it right before asking the
    // transport to jump, for an explicit jump only (a marker, a locator, Return to Start), never for a scrub or a
    // held cursor move. Without a following locate within 1.5 s the arm lapses.
    void armLocateGlide();

    // THE drive seam. Called from the owner's LOW-RATE poll (10 Hz), never faster: it stores the
    // snapshot, starts/stops the display frames on a play/stop transition, and requests a strip repaint
    // only when the rounded line x actually moved (plus the one guaranteed final strip on stop).
    void updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot, double outputLatencySeconds);

    void paint(juce::Graphics& g) override;

    // The beat the line is drawn at RIGHT NOW: the transport position offset backwards by the
    // output latency, clamped >= 0 (see the class comment). This is also what a LocalPlayheadClient
    // is handed — the beat, never a pixel, because the client maps it itself.
    double getDrawnBeat() const noexcept;
    // The same without a locate glide: where the line is heading. Page follow pages to this, at once.
    double getTargetBeat() const noexcept;

    // The x (in this component's coordinates, which share TimelineViewState's origin) the line is
    // drawn at RIGHT NOW, latency offset included. Recomputed from the stored snapshot + view state
    // on every call — it is not cached, so a zoom/scroll changes it immediately.
    int getLineX() const noexcept;

    // The part of this overlay the SHARED mapping still owns: everything, minus the local client's
    // region while one is active. Both paint() and the repaint strips are confined to it.
    juce::Rectangle<int> getSharedRegion() const noexcept;

    // The x the line was last DRAWN/REQUESTED at, in PIXEL space. Deliberately not re-derived from
    // the old beat: after a zoom the old beat maps somewhere new, but the stale pixels that must be
    // repainted are where the line actually was. This is what the old half of the union strip is
    // built from.
    int getLastRequestedLineX() const noexcept { return lineX_; }

    bool isPlayheadFrameRunning() const noexcept { return frames_ != nullptr && !framesEnded_; }

    // The frames of the current (or last) frame run, and the median gap between the last 64 of them in ms (0 under two
    // frames). Test mode reads them through the `timelinePlayhead` state; nothing else does.
    int getFrameCount() const noexcept { return frameCount_; }
    double getMedianFrameIntervalMs() const;

    // Test seams for a panel whose overlay a test cannot subclass: a replacement wall clock and one frame by hand.
    void setClockForTest(std::function<double()> clock) { testClock_ = std::move(clock); }
    void runFrameForTest() { onFrame(); }
    // Lets a glide play although the overlay is not on screen (no display frame reaches a headless component).
    static void setGlideOffScreenForTest(bool allow);

protected:
    // THE paint-count seam, and the repo's pattern for making a repaint budget testable: EVERY
    // repaint this component asks for goes through here and nowhere else. A test subclasses the
    // component, overrides this to count calls (and record the rect), and can then assert an exact
    // repaint budget headlessly — no peer, no message loop, no screenshot diffing. The default
    // implementation is the real repaint.
    virtual void requestRepaintStrip(juce::Rectangle<int> strip);

    // One display frame, protected so a test subclass can drive frames deterministically instead of waiting on the
    // display. Only ever runs while playing, previewing or gliding.
    void onFrame();

    // The wall clock in ms, hi-res. A test overrides it to place frames in time.
    virtual double nowMs() const;

private:
    // The repaint strip for a line at `x`: the line plus kStripHalfWidth px either side.
    juce::Rectangle<int> stripFor(int x) const noexcept;

    // Recomputes the line x and requests the union strip when it moved. `force` (the stop
    // transition) requests one strip even when it did not.
    void refreshLine(bool force);

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;
    LocalPlayheadClient* localClient_ = nullptr;
    juce::Rectangle<int> localRegion_;

    synth::TransportService::PositionSnapshot snapshot_{};
    double outputLatencySeconds_ = 0.0;
    PlayheadWrapCompensation wrap_;
    std::unique_ptr<PlayheadExtrapolator> extrapolator_;
    std::unique_ptr<PlayheadLocateGlide> glide_;
    bool haveLocateCount_ = false;
    std::uint32_t locateCount_ = 0;
    double evaluatedAtMs_ = 0.0; // the clock reading the drawn beat was last evaluated at
    std::function<double()> testClock_;
    std::unique_ptr<juce::Component> stateProbe_;

    // The frame source: present while frames should run. `framesEnded_` marks a run that finished inside its own frame
    // (a glide that landed while stopped): the attachment is then dropped from the message queue, never from inside
    // its own callback.
    std::unique_ptr<juce::VBlankAttachment> frames_;
    bool framesEnded_ = false;
    int frameCount_ = 0;
    std::array<double, 64> frameTimesMs_{};

    void startFrames();
    void stopFrames();
    void endFramesFromFrame();
    bool wantsFrames() const noexcept;

    double latencyBeats() const noexcept;
    void adoptSnapshot(const synth::TransportService::PositionSnapshot& snapshot);

    bool playing_ = false;
    bool previewActive_ = false;
    // Seeded (without requesting a repaint) by the first updateFromTransport: before that there is
    // no "old" line on screen to erase, so the first poll of a stopped transport must stay silent.
    bool hasLineX_ = false;
    int lineX_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelinePlayheadOverlay)
};

} // namespace synth::ui
