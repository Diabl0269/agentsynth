#pragma once

// DualIOJackGlide.h -- a card's jacks gliding when its Dual I/O changes which jack each raw channel sits on: the one
// Audio jack splits into Left and Right, and they merge back, with the cables on them following every frame. The card's
// layout is final at once; only where the jacks and cable ends are DRAWN trails it. docs/layout/animation.md, "Dual I/O
// split and merge".

#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class ModuleComponent;
namespace graph_editor_types {
struct VisibleCable;
}

class DualIOJackGlide {
public:
    static constexpr double kMs = 160.0;

    explicit DualIOJackGlide(ModuleComponent& card);
    ~DualIOJackGlide();
    DualIOJackGlide(const DualIOJackGlide&) = delete;
    DualIOJackGlide& operator=(const DualIOJackGlide&) = delete;

    /** One jack as paint() drew it (card-local). */
    struct Drawn {
        int visible = 0;
        juce::Point<float> centre;
        juce::String label;
    };
    /** paint() hands over the jacks it drew, so the next change starts from what is on screen. */
    void noteDrawn(bool isInput, std::vector<Drawn> jacks);

    /** Call while the card's cached port maps and the graph's cables still describe the old layout. */
    void captureBefore();
    /** True between captureBefore() and start(). */
    bool isCapturing() const noexcept { return pending_.valid; }
    /** Call once the card is laid out at the new layout: the jacks glide there, or land at once. */
    void start();

    bool isLive() const noexcept { return live_; }
    /** Where visible jack `visible` is drawn now, given where the layout puts it. */
    juce::Point<int> drawnJack(int visible, bool isInput, juce::Point<int> laidOut) const;
    /** 0..1: how much of a jack's label shows (a jack the change made fades its label in). */
    float labelAlpha(int visible, bool isInput) const;
    /** Card-local end of the cable between this card's raw `ownRaw` and (`otherNode`, `otherChannel`), or nullopt
     *  when the cable ends on its jack as drawn. */
    std::optional<juce::Point<float>> cableEnd(uint32_t otherNode, int otherChannel, int ownRaw, bool isInput) const;
    /** True while the glide moves the cable that ends on (`otherNode`, `otherChannel`) here. */
    bool carriesCable(uint32_t otherNode, int otherChannel, bool isInput) const;
    /** True while a glide on `src` or `dst` (either may be null) moves the audio cable `c` between them. */
    static bool carriesEdge(const ModuleComponent* src, const ModuleComponent* dst,
                            const juce::AudioProcessorGraph::Connection& c);
    /** Paints the jacks the change merged away, sliding into the jack that took them over and fading. */
    void paintGhosts(juce::Graphics& g, juce::Colour jack, juce::Colour label) const;

    /** The card's glide, made the first time a card whose Dual I/O can change paints; nullptr for any other card. */
    static DualIOJackGlide* forPainting(ModuleComponent& card);
    /** Where `card` draws visible jack `visible` now, card-local and on the canvas. */
    static juce::Point<int> drawnPort(ModuleComponent& card, int visible, bool isInput);
    static juce::Point<float> drawnPortOnCanvas(ModuleComponent& card, int visible, bool isInput);
    /** Moves an audio cable's canvas ends (`p1` on `src`, `p2` on `dst`) to where a running glide draws them. */
    static void glideCableEnds(const ModuleComponent& src, const ModuleComponent& dst,
                               const juce::AudioProcessorGraph::Connection& c, juce::Point<float>& p1,
                               juce::Point<float>& p2);
    /** Leaves out of a cable retract/grow diff the cables a running glide on one of `cards` carries. */
    static void dropCablesGlidesCarry(std::vector<graph_editor_types::VisibleCable>& cables,
                                      const juce::OwnedArray<ModuleComponent>& cards);
    /** paint()'s last step: records the outputs it drew and paints the ghosts over them. */
    void finishPaint(juce::Graphics& g, std::vector<Drawn> outputs, juce::Colour jack, juce::Colour label);

    /** Test seams: animate off screen; advance the running glide to `ms` after it began. */
    static void setAnimateOffScreenForTest(bool animate) noexcept;
    void stepForTest(double ms) { driver_.stepForTest(ms); }

private:
    using CableKey = std::tuple<uint32_t, int, int, bool>; // other node, other channel, own raw, is input
    struct Ghost {
        int raw = 0;
        juce::String label;
    };
    struct Side {
        std::vector<Drawn> record;              // as last drawn, or as laid out by the last change
        std::map<int, juce::Point<float>> from; // raw -> drawn place before the change
        std::map<int, juce::Point<float>> to;   // raw -> laid-out place after it
        std::map<int, int> headOf;              // visible jack -> its head raw in the new layout
        std::set<int> fresh;                    // visible jacks the change made
        std::vector<Ghost> ghosts;
    };
    struct Pending {
        bool valid = false;
        std::map<int, juce::Point<float>> centres[2]; // raw -> drawn place, outputs [0] and inputs [1]
        std::set<int> heads[2];                       // the raws that headed a jack
        std::vector<Ghost> jacks[2];                  // every drawn jack's head raw and label
        std::map<CableKey, juce::Point<float>> cables;
        std::map<const juce::Component*, int> childY; // each control's drawn top before the change
        bool onScreen = false;                        // the card had been drawn (or was mid-glide)
    };
    struct Slide {
        juce::Component::SafePointer<juce::Component> part;
        float dy = 0.0f; // where it was drawn minus where the layout puts it
    };

    Side& side(bool isInput) noexcept { return sides_[isInput ? 1 : 0]; }
    const Side& side(bool isInput) const noexcept { return sides_[isInput ? 1 : 0]; }
    std::optional<juce::Point<float>> drawnRaw(bool isInput, int raw) const;
    void layoutSide(bool isInput);
    void frame(float e);
    void land();
    void clearSlides();

    ModuleComponent& card_;
    Side sides_[2];
    Pending pending_;
    std::map<CableKey, juce::Point<float>> cableFrom_;
    std::vector<Slide> slides_;
    bool live_ = false;
    float e_ = 1.0f;
    juce::VBlankAnimatorUpdater updater_;
    synth::ui::AnimationDriver driver_;
};
