// MacroFoldAnimator.cpp
//
// The fold overlay. Every card keeps its FINAL state the moment the model changes (hit-tests, hulls, cable geometry
// all read it), so the animation is an illusion layered on top: the real cards that should not show yet are made
// invisible (so they cannot be clicked, focused or read by a screen reader either) and pictures of them fly between
// their place and their preview box on the closed card. Collapsing, the closed card stays hidden until the modules
// have landed and then takes over from the ghosts in a short hand-over; expanding, each module's card is shown the
// moment its ghost lands. Time-bounded: the driver runs for the fold's length and the canvas repaints only the part
// the fold moves. Reduce Motion replaces the flight with a plain fade of the cards that appear.
//
// A macro nested in a folding one is one module of its parent's plan and, when it is open, has a plan of its own with a
// delay: folding, its modules fly into its card first and the card then fades in as that box while the dashed border
// fades out (the hand-over a top-level macro does with its real card); unfolding, the box flies out to that card and
// fades as the nested macro's own modules start out of it. Each plan runs on its own clock, `localMs`.

#include "MacroFoldAnimator.h"

#include "UI/Layout/CableCurve.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/ZoomFrozenCachedImage.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"

#include <algorithm>

namespace mf = synth::ui::macro_fold;

namespace {
float smooth(float t) { return std::clamp(t, 0.0f, 1.0f); }
} // namespace

MacroFoldAnimator::~MacroFoldAnimator() {
    if (hooks_.updater != nullptr)
        driver_.stop(*hooks_.updater);
}

void MacroFoldAnimator::setHooks(Hooks hooks) { hooks_ = std::move(hooks); }

bool MacroFoldAnimator::wouldAnimate() const { return canAnimate() && !synth::ui::animationsOff(); }

bool MacroFoldAnimator::canAnimate() const { return forceAnimate_ || (hooks_.canAnimate && hooks_.canAnimate()); }

juce::Image MacroFoldAnimator::cachedPicture(juce::Component& comp) {
    if (auto* cache = dynamic_cast<synth::ui::ZoomFrozenCachedImage*>(comp.getCachedComponentImage()))
        return cache->lastRaster();
    return {};
}

void MacroFoldAnimator::paintDashedBorder(juce::Graphics& g, juce::Rectangle<float> border, juce::Colour colour,
                                          bool emphasised) {
    juce::Path outline;
    outline.addRoundedRectangle(border, 10.0f);
    juce::Path dashed;
    const float dashLengths[] = {6.0f, 4.0f};
    juce::PathStrokeType(emphasised ? 2.5f : 1.5f).createDashedStroke(dashed, outline, dashLengths, 2);
    g.setColour(colour.withAlpha(emphasised ? 0.9f : 0.6f));
    g.fillPath(dashed);
    if (emphasised) {
        g.setColour(colour);
        g.strokePath(outline, juce::PathStrokeType(2.5f));
    }
}

// ---- Geometry at the current time ----------------------------------------------------------------------------

// Collapse only: how far the real card has taken over from the ghosts, 0..1, over the last kHandoverMs.
float MacroFoldAnimator::handover(const Live& live) const noexcept {
    if (!live.plan.collapsing || reduced_)
        return 0.0f;
    return smooth(
        static_cast<float>((localMs(live) - (live.timeline.modulesEndMs() - mf::kHandoverMs)) / mf::kHandoverMs));
}

const MacroFoldAnimator::Live* MacroFoldAnimator::liveFor(const juce::String& macroId) const noexcept {
    for (const auto& live : lives_)
        if (live.plan.macroId == macroId)
            return &live;
    return nullptr;
}

// How much of a nested macro's box shows while its own plan runs: folding, it fades in as that plan hands over (its
// modules have reached their boxes on it); unfolding, it fades as that plan starts. A plain module, or a nested macro
// that is folded and has no plan of its own, always shows.
float MacroFoldAnimator::nestedAlpha(const Live& live, size_t index) const noexcept {
    const auto& m = live.plan.modules[index];
    const auto* nested = m.macroId.isEmpty() ? nullptr : liveFor(m.macroId);
    if (nested == nullptr || nested == &live)
        return 1.0f;
    if (live.plan.collapsing)
        return nested->plan.collapsing ? handover(*nested) : 1.0f;
    return 1.0f - smooth(static_cast<float>(localMs(*nested) / mf::kHandoverMs));
}

juce::Rectangle<float> MacroFoldAnimator::moduleRect(const Live& live, size_t index) const noexcept {
    const auto& m = live.plan.modules[index];
    const float e = live.timeline.eased(static_cast<int>(index), localMs(live));
    return live.plan.collapsing ? mf::lerpRect(m.rect.toFloat(), m.box, e) : mf::lerpRect(m.box, m.rect.toFloat(), e);
}

std::vector<juce::Rectangle<float>> MacroFoldAnimator::moduleRects(const Live& live) const {
    std::vector<juce::Rectangle<float>> out;
    out.reserve(live.plan.modules.size());
    for (size_t i = 0; i < live.plan.modules.size(); ++i)
        out.push_back(moduleRect(live, i));
    return out;
}

// The border holds every module in every frame: the card-to-border blend grown to take in each module's current rect.
juce::Rectangle<float> MacroFoldAnimator::outline(const Live& live) const noexcept {
    return mf::outlineRect(live.plan.hull.toFloat(), live.plan.card.toFloat(),
                           live.timeline.outlineToCard(localMs(live)), moduleRects(live));
}

int MacroFoldAnimator::indexOf(const Live& live, uint32_t nodeUid) const noexcept {
    if (nodeUid == 0)
        return -1;
    for (size_t i = 0; i < live.plan.modules.size(); ++i)
        if (live.plan.modules[i].nodeUid == nodeUid)
            return static_cast<int>(i);
    return -1;
}

int MacroFoldAnimator::ghostCount() const noexcept {
    int n = 0;
    for (const auto& live : lives_)
        for (size_t i = 0; i < live.plan.modules.size(); ++i)
            n += live.plan.collapsing || live.landed[i] == 0 ? 1 : 0;
    return n;
}

std::vector<juce::Rectangle<float>> MacroFoldAnimator::moduleRects(const juce::String& macroId) const {
    for (const auto& live : lives_)
        if (live.plan.macroId == macroId)
            return moduleRects(live);
    return {};
}

juce::Rectangle<float> MacroFoldAnimator::moduleRectFor(uint32_t nodeUid) const {
    for (const auto& live : lives_)
        if (const int i = indexOf(live, nodeUid); i >= 0)
            return moduleRect(live, static_cast<size_t>(i));
    return {};
}

std::optional<juce::Rectangle<float>> MacroFoldAnimator::outlineFor(const juce::String& macroId) const {
    for (const auto& live : lives_)
        if (live.plan.macroId == macroId)
            return outline(live);
    return std::nullopt;
}

// A nested macro waiting to unfold has no border yet (its box is still flying out of its parent): empty, so the canvas
// draws none.
std::optional<juce::Rectangle<int>> MacroFoldAnimator::expandOutlineFor(const juce::String& macroId) const {
    if (reduced_)
        return std::nullopt;
    for (const auto& live : lives_)
        if (live.plan.macroId == macroId && !live.plan.collapsing)
            return localMs(live) < 0.0 ? juce::Rectangle<int>() : outline(live).getSmallestIntegerContainer();
    return std::nullopt;
}

juce::Rectangle<float> MacroFoldAnimator::nestedRectFor(const juce::String& macroId) const {
    for (const auto& live : lives_)
        for (size_t i = 0; i < live.plan.modules.size(); ++i)
            if (live.plan.modules[i].macroId == macroId && macroId.isNotEmpty())
                return moduleRect(live, i);
    return {};
}

std::optional<std::pair<double, double>> MacroFoldAnimator::spanFor(const juce::String& macroId) const {
    if (const auto* live = liveFor(macroId))
        return std::make_pair(live->plan.delayMs, live->plan.delayMs + live->timeline.modulesEndMs());
    return std::nullopt;
}

bool MacroFoldAnimator::isHeld(uint32_t nodeUid) const {
    for (const auto& live : lives_)
        if (const int i = indexOf(live, nodeUid); i >= 0)
            return !live.plan.collapsing && live.landed[static_cast<size_t>(i)] == 0;
    return false;
}

// ---- Cables --------------------------------------------------------------------------------------------------

void MacroFoldAnimator::applyTo(std::vector<VisibleCable>& cables) const {
    for (const auto& live : lives_) {
        if (live.plan.collapsing || reduced_)
            continue;
        cables.erase(std::remove_if(cables.begin(), cables.end(),
                                    [this, &live](const VisibleCable& c) {
                                        return indexOf(live, c.id.srcUid) >= 0 || indexOf(live, c.id.dstUid) >= 0;
                                    }),
                     cables.end());
    }
}

// Where the end `p` of a cable on module `nodeUid` is drawn now: its place on the card, mapped into the card's flying
// rect. A module no plan flies stays put.
juce::Point<float> MacroFoldAnimator::cableEnd(uint32_t nodeUid, juce::Point<float> p) const {
    for (const auto& live : lives_)
        if (const int i = indexOf(live, nodeUid); i >= 0) {
            const auto from = live.plan.modules[static_cast<size_t>(i)].rect.toFloat();
            const auto now = moduleRect(live, static_cast<size_t>(i));
            if (from.isEmpty())
                return now.getCentre();
            return {now.getX() + (p.x - from.getX()) * now.getWidth() / from.getWidth(),
                    now.getY() + (p.y - from.getY()) * now.getHeight() / from.getHeight()};
        }
    return p;
}

// An inner cable rides its two modules and fades as this plan's ends reach their boxes. A cable from a nested macro's
// module to one of its parent's belongs to the nested plan, so it fades with that plan's own cables.
std::optional<MacroFoldAnimator::DrawnCable> MacroFoldAnimator::collapseCable(const Live& live,
                                                                              const VisibleCable& cable) const {
    const int a = indexOf(live, cable.id.srcUid), b = indexOf(live, cable.id.dstUid);
    if (a < 0 && b < 0)
        return std::nullopt;
    const double ms = localMs(live);
    const float reach =
        std::min(a >= 0 ? live.timeline.flight(a, ms) : 1.0f, b >= 0 ? live.timeline.flight(b, ms) : 1.0f);
    const float alpha = 1.0f - smooth((reach - 0.55f) / 0.45f);
    if (alpha <= 0.0f)
        return std::nullopt;
    DrawnCable out{cable, alpha};
    out.cable.p1 = cableEnd(cable.id.srcUid, cable.p1);
    out.cable.p2 = cableEnd(cable.id.dstUid, cable.p2);
    return out;
}

// A cable is not drawn until the module it touches has landed (the later one, when both ends are modules), then draws
// out of that module's port, the far end growing away from it.
std::optional<MacroFoldAnimator::DrawnCable> MacroFoldAnimator::expandCable(const Live& live,
                                                                            const VisibleCable& cable) const {
    const int a = indexOf(live, cable.id.srcUid), b = indexOf(live, cable.id.dstUid);
    if (a < 0 && b < 0)
        return std::nullopt;
    const double landA = a >= 0 ? live.plan.delayMs + live.timeline.landMs(a) : -1.0;
    const double landB = b >= 0 ? live.plan.delayMs + live.timeline.landMs(b) : -1.0;
    const double landed = std::max(landA, landB);
    const float draw = mf::Timeline::cableDraw(landed, elapsed_);
    if (elapsed_ < landed || draw <= 0.0f)
        return std::nullopt;
    DrawnCable out{cable, 1.0f};
    if (landA >= landB)
        out.cable.p2 = cable.p1 + (cable.p2 - cable.p1) * draw;
    else
        out.cable.p1 = cable.p2 + (cable.p1 - cable.p2) * draw;
    return out;
}

std::vector<MacroFoldAnimator::DrawnCable> MacroFoldAnimator::drawnCables(const Live& live) const {
    std::vector<DrawnCable> out;
    if (reduced_)
        return out;
    for (const auto& cable : live.plan.cables)
        if (auto drawn = live.plan.collapsing ? collapseCable(live, cable) : expandCable(live, cable))
            out.push_back(std::move(*drawn));
    return out;
}

std::vector<MacroFoldAnimator::DrawnCable> MacroFoldAnimator::drawnCables() const {
    std::vector<DrawnCable> out;
    for (const auto& live : lives_)
        for (auto& drawn : drawnCables(live))
            out.push_back(std::move(drawn));
    return out;
}

std::vector<MacroFoldAnimator::VisibleCable> MacroFoldAnimator::currentCables() const {
    return hooks_.visibleCables ? hooks_.visibleCables() : std::vector<VisibleCable>();
}

// ---- Painting ------------------------------------------------------------------------------------------------

void MacroFoldAnimator::paintModule(juce::Graphics& g, const Live& live, size_t index) const {
    const auto& m = live.plan.modules[index];
    // A landed module's real card is showing; a landed nested macro that unfolds next stays until its modules start.
    const bool unfoldsNext = m.macroId.isNotEmpty() && liveFor(m.macroId) != nullptr;
    if (!live.plan.collapsing && live.landed[index] != 0 && !unfoldsNext)
        return;
    const float shown = nestedAlpha(live, index);
    const auto rect = moduleRect(live, index);
    if (rect.isEmpty() || shown <= 0.0f)
        return;
    const float p = live.timeline.flight(static_cast<int>(index), localMs(live));
    const float toBox = live.plan.collapsing ? smooth((p - 0.6f) / 0.4f) : 1.0f - smooth(p / 0.4f);
    float pictureAlpha = (1.0f - toBox) * shown;
    float boxAlpha = (m.picture.isNull() ? 1.0f : toBox) * shown;
    if (live.plan.collapsing)
        boxAlpha *= 1.0f - handover(live);

    if (!m.picture.isNull() && pictureAlpha > 0.0f) {
        juce::Graphics::ScopedSaveState state(g);
        g.setOpacity(pictureAlpha);
        g.drawImage(m.picture, rect, juce::RectanglePlacement::stretchToFit);
    }
    if (boxAlpha > 0.0f && m.macroId.isNotEmpty()) {
        macro_preview::paintMacroBox(g, rect, m.colour, boxAlpha);
    } else if (boxAlpha > 0.0f) {
        const auto colour = hooks_.categoryColour ? hooks_.categoryColour(m.category) : juce::Colours::grey;
        g.setColour(colour.withAlpha(0.85f * boxAlpha));
        g.fillRoundedRectangle(rect, 1.5f);
    }
}

void MacroFoldAnimator::paint(juce::Graphics& g) const {
    if (reduced_)
        return;
    for (const auto& live : lives_) {
        if (!live.plan.collapsing && localMs(live) < 0.0)
            continue; // a nested macro waiting for its box to fly out of its parent
        if (live.plan.collapsing)
            paintDashedBorder(g, outline(live), live.plan.colour.withMultipliedAlpha(1.0f - handover(live)), false);
        if (hooks_.paintCable)
            for (const auto& drawn : drawnCables(live))
                hooks_.paintCable(g, drawn.cable, drawn.alpha);
        for (size_t i = 0; i < live.plan.modules.size(); ++i)
            paintModule(g, live, i);
    }
}

// Everything the fold draws now, with room for the chip and strips the canvas draws on an expanding border.
juce::Rectangle<int> MacroFoldAnimator::drawnArea() const {
    juce::Rectangle<float> area;
    for (const auto& live : lives_) {
        area = area.getUnion(outline(live));
        for (const auto& drawn : drawnCables(live))
            area = area.getUnion(synth::ui::cablePaintBounds(drawn.cable.p1, drawn.cable.p2));
    }
    return area.isEmpty() ? juce::Rectangle<int>() : area.getSmallestIntegerContainer().expanded(24);
}

// ---- Driving -------------------------------------------------------------------------------------------------

bool MacroFoldAnimator::arm(std::vector<Plan> plans) {
    land();
    plans.erase(std::remove_if(plans.begin(), plans.end(), [](const Plan& p) { return p.modules.empty(); }),
                plans.end());
    if (plans.empty() || !wouldAnimate())
        return false;

    reduced_ = synth::ui::prefersReducedMotion();
    total_ = 0.0;
    for (auto& plan : plans) {
        Live live;
        live.timeline.count = static_cast<int>(plan.modules.size());
        live.timeline.collapsing = plan.collapsing;
        live.timeline.withCables = !plan.cables.empty();
        live.landed.assign(plan.modules.size(), 0);
        total_ = std::max(total_, reduced_ ? mf::kFadeMs : plan.delayMs + live.timeline.totalMs());
        live.plan = std::move(plan);
        // Held back: invisible, so nothing can click, focus or announce a card that is not on screen yet. A plain fade
        // keeps the card visible at alpha 0 instead.
        const auto hold = [this](juce::Component* c) {
            if (c == nullptr)
                return;
            if (reduced_)
                c->setAlpha(0.0f);
            else
                c->setVisible(false);
        };
        if (live.plan.collapsing)
            hold(live.plan.cardComp.getComponent());
        else {
            for (const auto& m : live.plan.modules)
                hold(m.comp.getComponent());
            for (const auto& port : live.plan.ports)
                hold(port.getComponent());
        }
        lives_.push_back(std::move(live));
    }
    elapsed_ = 0.0;
    lastDrawn_ = drawnArea();
    if (hooks_.repaint)
        hooks_.repaint(); // drops the cable memo so an expanding fold's cables leave it
    if (hooks_.updater != nullptr)
        driver_.start(
            *hooks_.updater, total_, [](float t) { return t; }, [this](float t) { frameAt(t); },
            [this] { release(false); });
    return true;
}

// Per-frame effects on the real cards: the hand-over of a closing card, a landed module's card coming back, or the
// Reduce Motion fade.
void MacroFoldAnimator::applySideEffects() {
    for (auto& live : lives_) {
        if (reduced_) {
            const float u = smooth(static_cast<float>(elapsed_ / mf::kFadeMs));
            if (live.plan.collapsing) {
                if (auto* c = live.plan.cardComp.getComponent())
                    c->setAlpha(u);
            } else {
                for (const auto& m : live.plan.modules)
                    if (auto* c = m.comp.getComponent())
                        c->setAlpha(u);
            }
        } else if (live.plan.collapsing) {
            if (const float u = handover(live); u > 0.0f)
                if (auto* c = live.plan.cardComp.getComponent()) {
                    c->setVisible(true);
                    c->setAlpha(u);
                }
        } else {
            // The port widgets sit on the open border, so they wait until it has grown that far.
            if (localMs(live) >= live.timeline.modulesEndMs() * 0.6)
                for (const auto& port : live.plan.ports)
                    if (auto* c = port.getComponent())
                        c->setVisible(true);
            for (size_t i = 0; i < live.plan.modules.size(); ++i)
                if (live.landed[i] == 0 && live.timeline.flight(static_cast<int>(i), localMs(live)) >= 1.0f) {
                    live.landed[i] = 1;
                    if (auto* c = live.plan.modules[i].comp.getComponent())
                        c->setVisible(true);
                }
        }
    }
}

void MacroFoldAnimator::applyAtMs(double ms) {
    elapsed_ = ms;
    applySideEffects();
}

void MacroFoldAnimator::frameAt(float t) {
    if (lives_.empty())
        return;
    const auto before = lastDrawn_;
    applyAtMs(static_cast<double>(t) * total_);
    lastDrawn_ = drawnArea();
    const auto area = before.getUnion(lastDrawn_);
    if (t >= 1.0f)
        release(false); // the last frame: everything has landed
    else if (hooks_.repaintArea && !area.isEmpty())
        hooks_.repaintArea(area);
    else if (hooks_.repaint)
        hooks_.repaint();
}

void MacroFoldAnimator::stepFrameForTest(float t) { frameAt(t); }

void MacroFoldAnimator::land() noexcept { release(true); }

// `stopDriver` is false from the driver's own callbacks: it ends itself after its last frame, and stopping it from
// inside one would destroy the animator that is calling.
void MacroFoldAnimator::release(bool stopDriver) noexcept {
    if (stopDriver && hooks_.updater != nullptr)
        driver_.stop(*hooks_.updater);
    if (lives_.empty())
        return;
    for (auto& live : lives_) {
        if (live.plan.collapsing) {
            if (auto* c = live.plan.cardComp.getComponent()) {
                c->setVisible(true);
                c->setAlpha(1.0f);
            }
            continue;
        }
        for (const auto& m : live.plan.modules)
            if (auto* c = m.comp.getComponent()) {
                c->setVisible(true);
                c->setAlpha(1.0f);
            }
        for (const auto& port : live.plan.ports)
            if (auto* c = port.getComponent()) {
                c->setVisible(true);
                c->setAlpha(1.0f);
            }
    }
    lives_.clear();
    elapsed_ = total_ = 0.0;
    lastDrawn_ = {};
    if (hooks_.repaint)
        hooks_.repaint();
}
