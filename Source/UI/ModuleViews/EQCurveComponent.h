#pragma once

#include "FrequencyGrid.h"
#include "Modules/FX/ParametricEQModule.h"
#include "UI/Layout/FocusRing.h"
#include <array>
#include <functional>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

class AppUndoManager;
class ShortcutManager;
namespace synth::ui {
template <typename T>
class KeyedTween;
class KeyHold;
} // namespace synth::ui

/** Interactive response curve for ParametricEQModule: every one of its 24 band slots, in the traditional DAW idiom.
 *
 *  Mouse: double-click empty space adds a band (slots 1-4 by frequency, then the lowest free slot as a Bell),
 *  double-click a dot removes it, drag moves it (x frequency, y gain), scroll over it changes Q, right-click opens
 *  the type menu, Option-click solos a band, a drag on empty space boxes the bands inside it (Shift/Cmd/Ctrl adds,
 *  a bare click clears), Shift/Cmd-click toggles one, and dragging a selected band moves the whole selection.
 *  Keyboard (the curve is a Tab stop; the keys in the EQ section of
 *  Settings are rebindable, shown here with their defaults): Left/Right select the previous/next band, Alt+Left/Right
 *  a band's frequency, Alt+Up/Down its gain (held, they glide, one undo step; two arrows go diagonal),
 *  Shift+Left/Right extends the selection, Cmd+A selects every band, Up/Down its width (Q), S solos it, T opens its
 * type menu, + adds a band, Backspace removes it (like Delete: the next band takes the selection), Cmd+D duplicates it;
 * Ctrl+Shift+arrows move it by a finer step (Shift also makes a width step finer). Fixed keys: 1-4 select a band and
 * switch it on, Page Up/Down Q, Return switches it on or off, Delete removes the selection like Backspace, Escape ends
 * a solo this curve started. Every gesture is one undo step through onGestureStart/onGestureEnd. A screen reader reads
 * the selected band, e.g. "Band 7, Bell, 2.4 kHz, +3.0 dB, Q 0.7".
 *
 *  The mouse handlers are thin wrappers over addPointAt / removeBand / dragBandTo / nudgeBandQ, which are public so
 *  the interaction can be unit-tested directly. The bands are read through the module's band snapshots, never through
 *  isParameterExposed, so the curve shows slots the card does not.
 *
 *  Units: EQCurveInteraction.cpp (gestures), EQCurvePaint.cpp (drawing and spectrum), EQCurveTypeMenu.cpp (type and
 *  slope changes), EQCurveGlide.cpp (the poll and the glides), EQCurveKeyboard.cpp (keys and accessibility),
 *  EQCurveSelection.cpp (the selection, box select, group drag) and EQCurveKeyGlide.cpp (held arrows).
 */
class EQCurveComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::Timer {
public:
    /** Symmetric dB window: an EQ cuts as much as it boosts, so 0 dB sits dead centre. */
    static constexpr float minDb = -30.0f;
    static constexpr float maxDb = 30.0f;
    /** A band dot's radius, in pixels: the one number its painting, hit test, readout offset and type menu share. A
     *  selected or hovered dot grows by kDotActiveGrow. */
    static constexpr float kHandleRadius = 9.0f;
    static constexpr float kDotActiveGrow = 1.5f;
    /** The halo round a selected or hovered dot: its width beyond the ring and its alpha at full opacity. */
    static constexpr float kDotHaloWidth = 5.0f;
    static constexpr float kDotHaloAlpha = 0.45f;
    /** The accent ring of a selected or hovered dot: its stroke, standing just outside the dot's ring. */
    static constexpr float kDotAccentRingPx = 1.5f;
    /** The size of the band number inside a dot, in points. */
    static constexpr float kDotNumberPt = 10.0f;
    /** Click/hover radius around a band handle, in pixels. */
    static constexpr float kHitRadius = kHandleRadius + 4.0f;

    /** Told when a band setting, the type or the selection changed (a strip or pill beside the curve follows it). */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void eqCurveChanged() = 0;
    };
    void addListener(Listener* listener) { listeners_.add(listener); }
    void removeListener(Listener* listener) { listeners_.remove(listener); }

    explicit EQCurveComponent(ParametricEQModule& eq);
    ~EQCurveComponent() override;

    /** Resolves the keys through `manager` (null: the shipped defaults). Must outlive this. */
    void setShortcutManager(const ShortcutManager* manager) noexcept { shortcuts_ = manager; }
    const ShortcutManager* getShortcutManager() const noexcept { return shortcuts_; }
    /** The key that runs EQ action `actionId` as it reads now ("S", "Cmd+D"); empty when unbound. */
    static juce::String keyHint(const ShortcutManager* manager, const char* actionId);
    juce::String getTooltip() override;

    /** Called around every parameter-changing gesture so the host can bracket it in one undo step. Optional. */
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;

    void setShowSpectrum(bool show) {
        showSpectrum = show;
        repaint();
    }
    bool getShowSpectrum() const { return showSpectrum; }

    /** Lets an undo or redo glide the dots and the curve to the restored bands (EqBandGlide.h). Must outlive this. */
    void setUndoManager(AppUndoManager* manager);

    /** Where band `band`'s dot is drawn now (it trails the real band while a glide runs), and how opaque. */
    juce::Point<float> getDrawnBandPosition(int band) const { return bandPosition(dotBands_[(size_t)band]); }
    float getDrawnBandAlpha(int band) const noexcept { return dotAlpha_[(size_t)band]; }
    synth::ui::KeyedTween<juce::Point<float>>& getBandGlideForTest() noexcept;
    synth::ui::KeyedTween<juce::Point<float>>& getTypeMorphForTest() noexcept;
    /** The colour of band `band`'s dot ring now (blended while its type morphs). */
    juce::Colour getDotColourForTest(int band) const;
    /** The ring as painted: that colour moved to read on the surface and the spectrum's fill (EQBandKinds.h). */
    juce::Colour getDotRingColourForTest(int band) const;

    /** The band the keys act on (the cursor of the selection), or -1. */
    int getSelectedBand() const { return selectedBand; }
    /** Every selected band slot, ascending; includes the cursor. */
    const std::vector<int>& getSelectedBands() const noexcept;
    bool isBandSelected(int band) const;
    int getHoveredBand() const { return hoveredBand; }
    /** The module's real band settings as of the last poll (not the glide's drawn ones). */
    const ParametricEQModule::BandSnapshots& getBands() const noexcept { return lastBands; }
    ParametricEQModule& getModule() noexcept { return eqModule; }

    /** A press that lands inside `scope` (the card or window this curve sits in) on something that is not a control
     *  clears the selection, as an empty press inside the curve does; a press on the curve, a chip, a button or a knob
     *  keeps it. `scope` must outlive the curve or be destroyed with it. One scope per curve. */
    void deselectOnPressOutside(juce::Component& scope);

    /** Selects band `band` (-1 for none) alone, without switching it on; tells the listeners. */
    void setSelectedBand(int band);
    /** Selects `bands` (slots), with `cursor` the band the keys act on and `anchor` where Shift+arrows grow from. */
    void setSelection(std::vector<int> bands, int cursor, int anchor);
    /** Shift+Left/Right: grows the selection one band (in frequency order, on bands only) toward that side. */
    void extendSelectionRelative(int direction);
    /** Cmd+A: every band that is on. False when none is. */
    bool selectAllBands();
    /** Switches off every selected band as one undo step. With `selectNext` the selection then moves to the band
     *  after them in frequency (else the last one left; none when none is); without it the selection stays on them. */
    bool removeSelectedBands(bool selectNext);

    // ---------- coordinate mapping ----------

    static float freqToXStatic(float freq, float width) noexcept {
        return synth::ui::FrequencyGrid::freqToX(freq, width);
    }
    static float dbToYStatic(float db, float height) noexcept {
        return synth::ui::FrequencyGrid::dbToY(db, height, minDb, maxDb);
    }
    /** The plot: the component's bounds less the focus ring's stroke and gap (FocusRing.h focusRingInset), so the ring
     *  frames the whole plot and the grid, curve, spectrum and dots all sit inside it. Every mapping below,
     *  the hit tests and the dots are in component coordinates on this rectangle. */
    juce::Rectangle<float> plotArea() const;
    /** Component x of `freq` and y of `db` on the plot. */
    float xForFreq(float freq) const { return plotArea().getX() + freqToXStatic(freq, plotArea().getWidth()); }
    float yForDb(float db) const { return plotArea().getY() + dbToYStatic(db, plotArea().getHeight()); }
    float freqAtX(float x) const {
        const auto plot = plotArea();
        return synth::ui::FrequencyGrid::xToFreq(x - plot.getX(), plot.getWidth());
    }
    float gainAtY(float y) const {
        const auto plot = plotArea();
        return juce::jlimit(-ParametricEQModule::kMaxGainDb, ParametricEQModule::kMaxGainDb,
                            synth::ui::FrequencyGrid::yToDb(y - plot.getY(), plot.getHeight(), minDb, maxDb));
    }
    juce::Point<float> bandPosition(const ParametricEQModule::BandSnapshot& band) const {
        const auto plot = plotArea();
        return {plot.getX() + freqToXStatic(band.freqHz, plot.getWidth()),
                plot.getY() + dbToYStatic(band.gainDb, plot.getHeight())};
    }

    // ---------- interaction primitives (public so tests can drive them directly) ----------

    /** Nearest ENABLED band whose handle is within kHitRadius of `p`, or -1. */
    int hitTestBand(juce::Point<float> p) const;

    /** Adds a band at `p` and selects it; returns its slot, or -1 when all 24 are on. */
    int addPointAt(juce::Point<float> p);

    /** Adds a Bell at 0 dB in the widest gap between the bands that are on (the strip's + chip). -1 when full. */
    int addBandInLargestGap();

    /** Switches a band off; it keeps its settings. */
    void removeBand(int band);

    /** Moves a band's handle to `p`: x sets frequency, y sets gain. */
    void dragBandTo(int band, juce::Point<float> p);

    /** Multiplicatively widens (negative) or narrows (positive) a band's Q. */
    void nudgeBandQ(int band, float amount);

    /** Sets a band's type, and (for a cut) its slope when `slopeDbPerOct` is not 0, as one undo step. */
    void setBandTypeAndSlope(int band, synth::eq::BandType type, int slopeDbPerOct = 0);

    /** Copies the selected band into the lowest free slot, two semitones higher, as one undo step, and selects the
     *  copy. False when no band is selected or all 24 are on. */
    bool duplicateSelectedBand();

    // ---------- solo audition ----------

    /** The band the module is auditioning alone, or -1. */
    int getSoloBand() const noexcept { return eqModule.getSoloBand(); }
    /** Solos the selected band, or ends the solo when it already is. False when no enabled band is selected. */
    bool toggleSoloSelectedBand();
    /** Whether this curve started the solo now playing (it ends it when it is destroyed). */
    bool ownsSolo() const noexcept { return ownsSolo_; }
    /** How strongly band `band`'s dot shows as the soloed one (x, 0..1) and as dimmed by another's solo (y, 0..1). */
    juce::Point<float> getSoloLook(int band) const;
    synth::ui::KeyedTween<juce::Point<float>>& getSoloEaseForTest() noexcept;

    /** The bands whose value readout is meant to show now: the hovered band (or the one whose chip is hovered), the one
     *  being dragged, and a band a single-band change (drag, key nudge or hold, Q) touched within the last second.
     *  Selection alone shows nothing, and a group move shows nothing (several dots have no single value). */
    std::vector<int> getReadoutBands() const;
    /** The strip forwards the pointer over a band's chip as a hover of its dot; -1 when no chip is hovered. */
    void setChipHoveredBand(int band);
    int getChipHoveredBand() const { return chipHoveredBand_; }
    /** Replaces the clock (milliseconds) the readout's after-change hold counts on; a test steps it by hand. */
    void setReadoutClockForTest(std::function<double()> clock) { readoutClock_ = std::move(clock); }
    /** How strongly band `band`'s readout shows now (0..1): it fades in and out, never pops. */
    float getReadoutLook(int band) const;
    synth::ui::KeyedTween<juce::Point<float>>& getReadoutEaseForTest() noexcept;
    /** How lit band `band`'s halo and accent ring are now (0..1): they ease in and out with hover and selection. */
    float getHaloLook(int band) const;
    synth::ui::KeyedTween<juce::Point<float>>& getHaloEaseForTest() noexcept;

    /** Opens the type menu for the selected band, growing out of `screenArea`; nothing when none is selected. */
    void showTypeMenu(juce::Rectangle<int> screenArea);
    /** The same, growing out of the band's dot. */
    void showTypeMenuAtDot(int band);

    // ---------- keyboard ----------

    /** Moves every selected band by `octaves` in frequency and `dbDelta` in gain (each stops at its own limit) as one
     *  undo step; the dots ease to their new places. */
    bool nudgeSelectedBand(float octaves, float dbDelta);

    /** Selects band `band`, switching it on first if it is off. */
    void selectBand(int band);

    /** The bands as the chips show them: the bands that are on, plus the selected band while it is off, in frequency
     *  order (equal frequencies keep slot order). */
    std::vector<int> shownBandsInOrder() const;

    /** Moves the selection `delta` bands along shownBandsInOrder() (stopping at the first); the first shown band when
     *  none was selected. Past the last one it calls onSelectPastLast and keeps the selection. */
    void selectBandRelative(int delta);

    /** Called when selectBandRelative runs past the last shown band: the host puts focus on its + chip. */
    std::function<void()> onSelectPastLast;

    /** Switches the selected band on or off, keeping it selected. False when none is selected. */
    bool toggleSelectedBand();

    /** The selected band as spoken, e.g. "Band 7, Bell, 2.4 kHz, +3.0 dB, Q 0.7"; a summary when none is. */
    juce::String getAccessibilityValueText() const;

    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    /** True when `key` is the binding of EQ action `actionId` (the live ShortcutManager binding, else its default). A
     *  focused chip asks it for the keys it hands on to the curve (the + chip only forwards eqAddBand). */
    bool matchesAction(const juce::KeyPress& key, const char* actionId) const;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType) override;
    synth::ui::KeyHold& getKeyHoldForTest() noexcept;

    // ---------- mouse handling ----------

    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void timerCallback() override;
    void paint(juce::Graphics& g) override;

    /** Tests: one spectrum tick; true when it asked for a repaint. */
    bool spectrumTickForTest() { return spectrumTick(); }
    /** Tests: the plotted spectrum, numPoints values in dB. */
    const std::vector<float>& getSpectrumDbForTest() const { return spectrumMagnitudes; }

private:
    /** The band-poll half of the timer: adopts changed settings, gliding them when `restored`. */
    void syncBands(const ParametricEQModule::BandSnapshots& bands, float outputGainDb, bool restored);
    bool spectrumTick();

    static constexpr int numPoints = 512;
    static constexpr int fftOrder = 10; // 1024-point FFT
    static constexpr int fftSize = 1 << fftOrder;
    /** How much of a dot's opacity another band's solo takes away. */
    static constexpr float kSoloDim = 0.6f;
    /** One wheel notch (deltaY == 1) doubles/halves Q at 1.0; scaled down for finer control. */
    static constexpr float kQScrollSensitivity = 1.5f;
    static constexpr float kSpecMinDb = -80.0f;
    static constexpr float kSpecMaxDb = 0.0f;
    /** Peak below this counts as silence, so an idle patch stops repainting the spectrum. */
    static constexpr float kSilenceThreshold = 1.0e-5f;
    /** Per tick, how far each point moves toward its reading (silence reads the floor), and how close to the floor it
     *  snaps there. */
    static constexpr float kSpectrumSmoothing = 0.3f;
    static constexpr float kSpectrumRestDb = 0.05f;

    bool ensureSelectedBand();
    bool matchesStepAction(const juce::KeyPress& key, const char* actionId, bool& fine) const;
    bool handleActionKey(const juce::KeyPress& key);
    // Held arrows (EQCurveKeyGlide.cpp)
    bool handleFineMoveKey(const juce::KeyPress& key);
    bool pressMove(int keyCode, float octaves, float dbDelta, bool fine);
    bool beginKeyHold(int keyCode, bool fine);
    void applyKeyHold();
    void endKeyHold();
    std::vector<int> movableSelection();
    /** Moves the selected bands to `octaves`/`db` away from their `start` places, each clamped on its own. */
    bool moveBandsFrom(const std::vector<std::pair<int, juce::Point<float>>>& start, float octaves, float db);
    // Selection (EQCurveSelection.cpp)
    std::vector<int> bandsByFrequency() const;
    void selectNextAfterRemoval(float removedFreqHz);
    std::vector<int> bandsInBox(juce::Rectangle<float> box) const;
    void dragBoxTo(juce::Point<float> position);
    void dragSelectionTo(juce::Point<float> position);
    bool handleFixedKey(const juce::KeyPress& key);
    void startSolo(int band);
    void endSolo();
    void followSelectionSolo();
    /** Retargets every band's readout fade to getReadoutBands() (hover, drag and selection changes call it). */
    void syncReadouts();
    /** Retargets every dot's solo look to the module's solo and tells the listeners when it changed. */
    void syncSolo();
    int addBandAt(float freqHz, float gainDb);
    void refreshAccessibilityValue();
    void beginGesture() {
        if (onGestureStart)
            onGestureStart();
    }
    void endGesture() {
        if (onGestureEnd)
            onGestureEnd();
    }
    /** Pulls fresh band values from the module and redraws at once, so a drag tracks the pointer. */
    void refreshFromModule();
    static bool bandsDiffer(const ParametricEQModule::BandSnapshots& a, const ParametricEQModule::BandSnapshots& b);
    void recomputeMagnitudes();

    bool updateSpectrum();
    void paintSpectrum(juce::Graphics& g, float w, float h, juce::Colour colour);
    void paintGrid(juce::Graphics& g, float w, float h) const;
    void paintCurve(juce::Graphics& g, float w, float h) const;
    void paintDots(juce::Graphics& g, juce::Rectangle<float> plot) const;
    void paintReadout(juce::Graphics& g, int band, juce::Rectangle<float> plot, float look) const;
    juce::Colour dotColour(int band) const;
    juce::Colour dotRingColour(int band) const;

    ParametricEQModule& eqModule;

    std::vector<float> magnitudes;
    std::vector<float> morphFrom_; // the curve as drawn when a band's type last changed
    ParametricEQModule::BandSnapshots lastBands{};
    float lastOutputGainDb = 0.0f;
    class OutsidePress; // the press-outside listener of deselectOnPressOutside, in EQCurveInteraction.cpp
    std::shared_ptr<juce::MouseListener> outsidePress_; // a shared_ptr so the type may stay incomplete here
    struct HoldState; // the selection, the box and the key hold (their headers reach far), in EQCurveHoldState.h
    std::unique_ptr<HoldState> hold_;
    struct GlideState; // EqBandGlide + RestoreWatch, defined in EQCurveGlide.cpp (their headers reach far)
    std::unique_ptr<GlideState> glide_;
    ParametricEQModule::BandSnapshots dotBands_{}; // as drawn now
    std::array<float, ParametricEQModule::kMaxBands> dotAlpha_{};
    std::array<synth::eq::BandType, ParametricEQModule::kMaxBands> morphFromType_{};
    float typeMorph_ = 0.0f;
    AppUndoManager* undoManager_ = nullptr;
    juce::ListenerList<Listener> listeners_;

    const ShortcutManager* shortcuts_ = nullptr;
    bool ownsSolo_ = false;
    int ownedSoloBand_ = -1;
    int selectedBand = -1;
    bool collapseOnRelease_ = false; // a press on a selected band of a group settles on release
    std::vector<std::pair<int, juce::Point<float>>> dragStart_; // (slot, (freq, gain)) of the dragged group
    bool glideMoves_ = false;                                   // the next sync eases moved dots (a keyboard step)
    juce::String announcedValueText;
    int hoveredBand = -1;
    int chipHoveredBand_ = -1;
    std::array<double, ParametricEQModule::kMaxBands> readoutHoldUntilMs_{}; // per band: show its readout until then
    std::function<double()> readoutClock_;
    void holdReadout(int band);
    int dragBand = -1;
    bool gestureActive = false;

    // The spectrum is the curve's backdrop by design, so it defaults on; once silence has let it fall to the floor,
    // updateSpectrum() stops asking for repaints, so it costs nothing when nothing is playing.
    bool showSpectrum = true;
    bool spectrumWasActive = false;
    juce::dsp::FFT fft{fftOrder};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData;
    std::vector<float> spectrumMagnitudes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQCurveComponent)
};
