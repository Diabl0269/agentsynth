// PartsMoveWithContainerTests.cpp -- the guard for the bug kind "motion-out-of-step": a container that grows or shrinks
// with its parts (a card whose height glides while the knobs and jacks it gains or loses come and go) has the parts
// move IN STEP with it. The parts start on the container's first frame and land on its last: an arriving part already
// shows a quarter into the glide, a leaving one is still drawn then, and none of them ends later than the container.
//
// partsWithContainer() is the registry, one row per surface. A new surface that grows or shrinks with its parts
// adds its row in the same change; a row that fails is fixed (the parts ride the container's duration), never skipped.
// Each row names the surface, starts the container change with the animated paths forced on, steps the container and
// its parts to the same elapsed time, and reports how much of each arriving and leaving part is drawn.

#include "../Graph/CardBody/MacroCountRideRig.h"
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace macro_ride_test;

namespace {

struct PartDrawn {
    std::string name;
    bool arriving; // else leaving
    float amount;  // 0 not drawn .. 1 fully drawn
};

class Surface {
public:
    virtual ~Surface() = default;
    /** Triggers the container change (animation forced on); the container and its parts are at their first frame. */
    virtual void start() = 0;
    /** Both the container and its parts, `ms` into the change. */
    virtual void stepTo(double ms) = 0;
    /** The container's own duration. */
    virtual double containerMs() const = 0;
    /** How much of every arriving and leaving part is drawn now. */
    virtual std::vector<PartDrawn> parts() = 0;
    /** True when the container has landed on its final size and nothing of the change still runs. */
    virtual bool landed() = 0;
};

// ---- Macros count: the knobs (swap motion) and the jacks (jack fade) ride the card's height glide ----

class MacrosCount : public Surface {
public:
    enum class Part { Knobs, Jacks };
    MacrosCount(Part part, bool grow)
        : part_(part)
        , grow_(grow)
        , id_(rig_.bank(grow ? kTo : kFrom))
        , finalHeight_(Rig::heightAt(grow ? kFrom : kTo)) {}

    void start() override { rig_.arrowTo(id_, grow_ ? kFrom : kTo); }
    void stepTo(double ms) override { rig_.stepTo(id_, ms); }
    double containerMs() const override { return glideMs(); }

    std::vector<PartDrawn> parts() override {
        std::vector<PartDrawn> drawn;
        const int lo = std::min(kFrom, kTo);
        const int hi = std::max(kFrom, kTo);
        if (part_ == Part::Jacks) {
            auto* jacks = rig_.card(id_)->heightGlideForTest().jacks();
            for (int jack = lo; jack < hi; ++jack)
                drawn.push_back(
                    {"jack " + std::to_string(jack), grow_, jacks != nullptr ? jacks->jackAlpha(jack) : 0.0f});
            return drawn;
        }
        if (grow_) {
            for (const auto& knob : knobsBetween(rig_, id_, lo, hi))
                for (auto* part : {knob.widget, knob.label})
                    drawn.push_back({knob.id.toStdString(), true,
                                     part->isVisible() ? std::min(part->getAlpha(), (float)scaleOf(*part)) : 0.0f});
            return drawn;
        }
        // Leaving knobs are pictures (ghosts) shrinking away: drawn as long as one is on the card.
        const auto ghosts = synth::cardbody_test::swapGhostRects(*rig_.card(id_));
        const int leaving = hi - lo;
        for (int i = 0; i < leaving; ++i)
            drawn.push_back({"leaving knob " + std::to_string(i), false,
                             (size_t)i < ghosts.size() && ghosts[(size_t)i].getHeight() > 0.5f ? 0.5f : 0.0f});
        return drawn;
    }

    bool landed() override {
        auto& card = *rig_.card(id_);
        const bool jacksDone =
            card.heightGlideForTest().jacks() == nullptr || !card.heightGlideForTest().jacks()->isFading();
        return !card.heightGlideForTest().isGliding() && !synth::cardbody_test::isSwapRunning(card) && jacksDone &&
               card.getHeight() == finalHeight_;
    }

private:
    RideGuard guard_;
    Rig rig_;
    Part part_;
    bool grow_;
    NodeID id_;
    int finalHeight_;
};

struct Entry {
    const char* name;
    std::function<std::unique_ptr<Surface>()> make;
};

const std::vector<Entry>& partsWithContainer() {
    using P = MacrosCount::Part;
    static const std::vector<Entry> entries{
        {"Macros count: knobs arrive (count up)", [] { return std::make_unique<MacrosCount>(P::Knobs, true); }},
        {"Macros count: knobs leave (count down)", [] { return std::make_unique<MacrosCount>(P::Knobs, false); }},
        {"Macros count: jacks arrive (count up)", [] { return std::make_unique<MacrosCount>(P::Jacks, true); }},
        {"Macros count: jacks leave (count down)", [] { return std::make_unique<MacrosCount>(P::Jacks, false); }},
    };
    return entries;
}

} // namespace

TEST(PartsMoveWithContainer, EverySurfaceStartsItsPartsWithTheContainerAndLandsThemOnItsLastFrame) {
    ASSERT_FALSE(partsWithContainer().empty());
    for (const auto& entry : partsWithContainer()) {
        SCOPED_TRACE(entry.name);
        auto surface = entry.make();
        surface->start();
        const double ms = surface->containerMs();
        ASSERT_GT(ms, 0.0);

        surface->stepTo(0.0);
        EXPECT_FALSE(surface->parts().empty()) << "the registry row reports parts";

        surface->stepTo(ms * 0.25);
        for (const auto& part : surface->parts())
            EXPECT_GT(part.amount, 0.0f) << part.name << (part.arriving ? " (arriving)" : " (leaving)")
                                         << ": a quarter into the container's glide it has to be drawn already";

        surface->stepTo(ms);
        EXPECT_TRUE(surface->landed()) << "on the container's last frame the container and every part have landed";
        for (const auto& part : surface->parts())
            EXPECT_FLOAT_EQ(part.amount, part.arriving ? 1.0f : 0.0f) << part.name << " at the container's end";
    }
}
