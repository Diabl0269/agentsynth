// DualIOJackGlide.cpp -- see the header. A raw channel is what keeps its identity across a Dual I/O change (raw 0 and
// the right leg), not a jack: a split gives the right leg a jack of its own, a merge folds it back into the Audio jack.
// So every raw that sits on a drawn jack eases from where it was drawn to where the new layout puts it, a jack is drawn
// where its head raw is, and a cable end is drawn where the raw it lands on is. A jack whose head raw the change folded
// into another jack (the Right of a merge) is drawn as a ghost that slides into its new jack and fades; a jack the
// change made (the Right of a split) leaves the shared jack and fades its label in. The glide is one 160 ms
// easeOutCubic on one clock, so a cable never leaves its jack. Under Reduce Motion, Animations Off, or off screen it
// lands at once.
#include "DualIOJackGlide.h"
#include "AudioEngine/AudioEngine.h"
#include "ModuleComponent.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>

namespace {
bool animateOffScreen = false;

juce::Point<float> lerp(juce::Point<float> a, juce::Point<float> b, float e) { return a + (b - a) * e; }

// A raw the layout puts on a jack: a reserved or hidden channel maps nowhere (role Other, not a head).
bool onAJack(const LogicalPort& p) { return p.isPolyGroupHead || p.role != PortRole::Other; }

std::vector<LogicalPort> mapOf(const ModuleBase& mb, bool isInput) {
    std::vector<LogicalPort> map;
    const int raws = isInput ? mb.getTotalNumInputChannels() : mb.getTotalNumOutputChannels();
    for (int raw = 0; raw < raws; ++raw)
        map.push_back(isInput ? mb.mapInputChannel(raw) : mb.mapOutputChannel(raw));
    return map;
}

// The raw a drawn jack is identified by: the lowest group head on it.
std::map<int, int> headsOf(const std::vector<LogicalPort>& map) {
    std::map<int, int> heads;
    for (int raw = 0; raw < (int)map.size(); ++raw)
        if (map[(size_t)raw].isPolyGroupHead && heads.count(map[(size_t)raw].visibleJackIndex) == 0)
            heads[map[(size_t)raw].visibleJackIndex] = raw;
    return heads;
}
} // namespace

DualIOJackGlide::DualIOJackGlide(ModuleComponent& card)
    : card_(card)
    , updater_(&card) {}

DualIOJackGlide::~DualIOJackGlide() { driver_.stop(updater_); }

void DualIOJackGlide::setAnimateOffScreenForTest(bool animate) noexcept { animateOffScreen = animate; }

void DualIOJackGlide::noteDrawn(bool isInput, std::vector<Drawn> jacks) { side(isInput).record = std::move(jacks); }

std::optional<juce::Point<float>> DualIOJackGlide::drawnRaw(bool isInput, int raw) const {
    const auto& s = side(isInput);
    const auto to = s.to.find(raw);
    if (to == s.to.end())
        return std::nullopt;
    const auto from = s.from.find(raw);
    return from == s.from.end() ? to->second : lerp(from->second, to->second, e_);
}

// Reads the old layout: the card's cached port maps are still the old ones and the graph still has the old cables (a
// click reaches here from the parameter callback before any rewiring; an undo or redo writes parameters before it
// reconciles the cables). While a glide runs, everything starts from where it is drawn mid-way, so a second Cmd+Z
// continues from the screen; at rest, from the jacks as paint() last drew them.
void DualIOJackGlide::captureBefore() {
    pending_ = {};
    auto* mb = dynamic_cast<ModuleBase*>(card_.module);
    if (mb == nullptr)
        return;
    pending_.valid = true;
    for (const bool isInput : {false, true}) {
        const auto& oldMap = isInput ? card_.cachedInputPortMap : card_.cachedOutputPortMap;
        const auto& s = side(isInput);
        const int k = isInput ? 1 : 0;
        std::map<int, const Drawn*> drawn;
        for (const auto& d : s.record)
            drawn[d.visible] = &d;
        const auto heads = headsOf(oldMap);
        for (int raw = 0; raw < (int)oldMap.size(); ++raw) {
            const auto& port = oldMap[(size_t)raw];
            if (!onAJack(port))
                continue;
            if (live_) {
                if (const auto p = drawnRaw(isInput, raw))
                    pending_.centres[k][raw] = *p;
            } else if (const auto it = drawn.find(port.visibleJackIndex); it != drawn.end()) {
                pending_.centres[k][raw] = it->second->centre;
            }
        }
        for (const auto& [visible, head] : heads) {
            pending_.heads[k].insert(head);
            if (const auto it = drawn.find(visible); it != drawn.end())
                pending_.jacks[k].push_back({head, it->second->label});
        }
    }
    pending_.onScreen = live_ || !sides_[0].record.empty() || !sides_[1].record.empty();
    for (auto* child : card_.getChildren()) // drawn top: a slide still running is where it is drawn
        pending_.childY[child] = child->getY() + juce::roundToInt(child->getTransform().getTranslationY());
    const auto self = card_.nodeId;
    for (const auto& c : card_.owner.getAudioEngine().getGraph().getConnections()) {
        if (c.source.isMIDI() || c.destination.isMIDI())
            continue;
        for (const bool isInput : {false, true}) {
            const auto& own = isInput ? c.destination : c.source;
            const auto& other = isInput ? c.source : c.destination;
            if (own.nodeID != self)
                continue;
            const CableKey key{other.nodeID.uid, other.channelIndex, own.channelIndex, isInput};
            std::optional<juce::Point<float>> at;
            if (live_)
                at = cableEnd(other.nodeID.uid, other.channelIndex, own.channelIndex, isInput);
            if (!at.has_value())
                if (const auto it = pending_.centres[isInput ? 1 : 0].find(own.channelIndex);
                    it != pending_.centres[isInput ? 1 : 0].end())
                    at = it->second;
            if (at.has_value())
                pending_.cables[key] = *at;
        }
    }
}

// The new layout's drawn jacks (the record the next change starts from), each raw's laid-out place, where each raw
// starts from, the jacks the change made and the ghosts of the ones it folded away.
void DualIOJackGlide::layoutSide(bool isInput) {
    auto& s = side(isInput);
    const int k = isInput ? 1 : 0;
    auto* mb = dynamic_cast<ModuleBase*>(card_.module);
    const auto map = mapOf(*mb, isInput);
    std::vector<int> visibleJacks;
    if (isInput) {
        visibleJacks = card_.drawnInputJackIndices();
    } else {
        for (int i = 0; i < mb->getVisibleOutputPortCount(); ++i)
            visibleJacks.push_back(i);
    }
    s = Side{};
    for (const int v : visibleJacks)
        s.record.push_back({v, card_.getPortCenter(v, isInput).toFloat(),
                            isInput ? mb->getInputPortLabel(v) : mb->getOutputPortLabel(v)});
    std::map<int, juce::Point<float>> laidOut;
    for (const auto& d : s.record)
        laidOut[d.visible] = d.centre;
    for (const auto& [visible, head] : headsOf(map)) {
        if (laidOut.count(visible) == 0)
            continue;
        s.headOf[visible] = head;
        if (pending_.heads[k].count(head) == 0)
            s.fresh.insert(visible);
    }
    for (int raw = 0; raw < (int)map.size(); ++raw) {
        if (!onAJack(map[(size_t)raw]))
            continue;
        const auto at = laidOut.find(map[(size_t)raw].visibleJackIndex);
        if (at == laidOut.end())
            continue;
        s.to[raw] = at->second;
        const auto was = pending_.centres[k].find(raw);
        if (was != pending_.centres[k].end() && was->second.getDistanceFrom(at->second) > 0.5f)
            s.from[raw] = was->second;
    }
    for (const auto& old : pending_.jacks[k]) {
        const bool stillHeads = std::any_of(s.headOf.begin(), s.headOf.end(),
                                            [&old](const auto& entry) { return entry.second == old.raw; });
        if (!stillHeads && s.to.count(old.raw) != 0 && s.from.count(old.raw) != 0)
            s.ghosts.push_back(old);
    }
}

void DualIOJackGlide::start() {
    if (!pending_.valid)
        return;
    driver_.stop(updater_);
    live_ = false;
    e_ = 1.0f;
    layoutSide(false);
    layoutSide(true);
    cableFrom_ = std::move(pending_.cables);
    clearSlides();
    for (auto* child : card_.getChildren())
        if (const auto it = pending_.childY.find(child);
            pending_.onScreen && it != pending_.childY.end() && it->second != child->getY())
            slides_.push_back({child, (float)(it->second - child->getY())});
    pending_ = {};
    const bool moves = !sides_[0].from.empty() || !sides_[1].from.empty() || !cableFrom_.empty() || !slides_.empty();
    const bool animate = (animateOffScreen || (card_.getParentComponent() != nullptr && card_.isShowing())) &&
                         !synth::ui::animationsOff() && !synth::ui::prefersReducedMotion();
    if (!moves || !animate) {
        land();
        return;
    }
    live_ = true;
    frame(0.0f); // the first frame is drawn where things were
    driver_.start(
        updater_, kMs, [](float t) { return synth::ui::easeOutCubic(t); }, [this](float e) { frame(e); },
        [this] { land(); });
}

// The controls below the jacks move by a row when the jacks split or merge: each is drawn from where it was, by a
// transform (its bounds are final at once, like the card body's swap motion), so nothing jumps under a gliding jack.
void DualIOJackGlide::frame(float e) {
    e_ = e;
    for (auto& slide : slides_)
        if (slide.part != nullptr)
            slide.part->setTransform(juce::AffineTransform::translation(0.0f, slide.dy * (1.0f - e)));
    card_.repaint();
    card_.owner.notifyModuleContentChanged(); // the cable ends move with the jacks
}

void DualIOJackGlide::land() {
    const bool wasLive = live_;
    live_ = false;
    e_ = 1.0f;
    for (auto& s : sides_) {
        s.from.clear();
        s.fresh.clear();
        s.ghosts.clear();
    }
    cableFrom_.clear();
    clearSlides();
    if (wasLive) {
        card_.repaint();
        card_.owner.notifyModuleContentChanged();
    }
}

juce::Point<int> DualIOJackGlide::drawnJack(int visible, bool isInput, juce::Point<int> laidOut) const {
    if (!live_)
        return laidOut;
    const auto& s = side(isInput);
    const auto head = s.headOf.find(visible);
    if (head == s.headOf.end())
        return laidOut;
    const auto from = s.from.find(head->second);
    if (from == s.from.end())
        return laidOut;
    return lerp(from->second, laidOut.toFloat(), e_).roundToInt();
}

float DualIOJackGlide::labelAlpha(int visible, bool isInput) const {
    return live_ && side(isInput).fresh.count(visible) != 0 ? e_ : 1.0f;
}

// A cable keeps its place across the change by what it comes from: a merge can move the right leg's cable onto the
// surviving jack's raw (a different raw, the same cable to the eye), so the exact match comes first, then the only old
// cable from that same channel, then the raw it lands on.
std::optional<juce::Point<float>> DualIOJackGlide::cableEnd(uint32_t otherNode, int otherChannel, int ownRaw,
                                                            bool isInput) const {
    if (!live_)
        return std::nullopt;
    const auto& s = side(isInput);
    const auto to = s.to.find(ownRaw);
    if (to == s.to.end())
        return std::nullopt;
    std::optional<juce::Point<float>> from;
    if (const auto exact = cableFrom_.find({otherNode, otherChannel, ownRaw, isInput}); exact != cableFrom_.end()) {
        from = exact->second;
    } else {
        int matches = 0;
        for (const auto& [key, at] : cableFrom_)
            if (std::get<0>(key) == otherNode && std::get<1>(key) == otherChannel && std::get<3>(key) == isInput) {
                ++matches;
                from = at;
            }
        if (matches != 1)
            from.reset();
    }
    if (!from.has_value())
        return drawnRaw(isInput, ownRaw);
    return lerp(*from, to->second, e_);
}

bool DualIOJackGlide::carriesCable(uint32_t otherNode, int otherChannel, bool isInput) const {
    if (!live_)
        return false;
    return std::any_of(cableFrom_.begin(), cableFrom_.end(), [&](const auto& entry) {
        return std::get<0>(entry.first) == otherNode && std::get<1>(entry.first) == otherChannel &&
               std::get<3>(entry.first) == isInput;
    });
}

void DualIOJackGlide::paintGhosts(juce::Graphics& g, juce::Colour jack, juce::Colour label) const {
    if (!live_)
        return;
    const float alpha = 1.0f - e_;
    for (const bool isInput : {false, true})
        for (const auto& ghost : side(isInput).ghosts) {
            const auto p = drawnRaw(isInput, ghost.raw);
            if (!p.has_value())
                continue;
            g.setColour(jack.withMultipliedAlpha(alpha));
            g.fillEllipse(p->x - 5.0f, p->y - 5.0f, 10.0f, 10.0f);
            g.setColour(label.withMultipliedAlpha(alpha));
            const auto text = isInput ? juce::Rectangle<float>(p->x + 10.0f, p->y - 10.0f, 60.0f, 20.0f)
                                      : juce::Rectangle<float>(p->x - 70.0f, p->y - 10.0f, 60.0f, 20.0f);
            g.drawText(ghost.label, text, isInput ? juce::Justification::left : juce::Justification::right, false);
        }
}

// Made lazily so a card that never paints (or has no Dual I/O, or hosts several output buses, whose strip owns that
// motion) carries no glide.
DualIOJackGlide* DualIOJackGlide::forPainting(ModuleComponent& card) {
    auto* mb = dynamic_cast<ModuleBase*>(card.module);
    if (card.dualIOGlide_ == nullptr && mb != nullptr && mb->hasDualIOParameter() && card.hostedOutputs_ == nullptr)
        card.dualIOGlide_ = std::make_unique<DualIOJackGlide>(card);
    return card.dualIOGlide_.get();
}

juce::Point<int> DualIOJackGlide::drawnPort(ModuleComponent& card, int visible, bool isInput) {
    const auto laidOut = card.getPortCenter(visible, isInput);
    const auto* glide = card.getDualIOGlide();
    return glide != nullptr ? glide->drawnJack(visible, isInput, laidOut) : laidOut;
}

juce::Point<float> DualIOJackGlide::drawnPortOnCanvas(ModuleComponent& card, int visible, bool isInput) {
    return (card.getBounds().getPosition() + drawnPort(card, visible, isInput)).toFloat();
}

// A cable end lands on the raw it is wired to, which can trail its jack while the jacks split or merge; MIDI ends sit
// on the MIDI jack, which never moves with Dual I/O.
void DualIOJackGlide::glideCableEnds(const ModuleComponent& src, const ModuleComponent& dst,
                                     const juce::AudioProcessorGraph::Connection& c, juce::Point<float>& p1,
                                     juce::Point<float>& p2) {
    if (c.source.isMIDI() || c.destination.isMIDI())
        return;
    const auto move = [](const ModuleComponent& card, uint32_t other, int otherCh, int ownRaw, bool isInput,
                         juce::Point<float>& end) {
        const auto* glide = card.getDualIOGlide();
        if (const auto at = glide != nullptr ? glide->cableEnd(other, otherCh, ownRaw, isInput) : std::nullopt)
            end = card.getBounds().getPosition().toFloat() + *at;
    };
    move(src, c.destination.nodeID.uid, c.destination.channelIndex, c.source.channelIndex, false, p1);
    move(dst, c.source.nodeID.uid, c.source.channelIndex, c.destination.channelIndex, true, p2);
}

void DualIOJackGlide::finishPaint(juce::Graphics& g, std::vector<Drawn> outputs, juce::Colour jack,
                                  juce::Colour label) {
    noteDrawn(false, std::move(outputs));
    paintGhosts(g, jack, label);
}

// A merge re-points the Right leg's cable onto the surviving jack, so its id changes and the retract would see one
// cable go and another come; the glide moves it with its jack instead, so it is left out of the diff on both sides.
void DualIOJackGlide::dropCablesGlidesCarry(std::vector<graph_editor_types::VisibleCable>& cables,
                                            const juce::OwnedArray<ModuleComponent>& cards) {
    for (auto* card : cards) {
        const auto* glide = card != nullptr ? card->getDualIOGlide() : nullptr;
        if (glide == nullptr || !glide->isLive())
            continue;
        const auto uid = card->getNodeId().uid;
        const auto carried = [&](const graph_editor_types::VisibleCable& c) {
            return (c.id.dstUid == uid && glide->carriesCable(c.id.srcUid, c.id.srcPort, true)) ||
                   (c.id.srcUid == uid && glide->carriesCable(c.id.dstUid, c.id.dstPort, false));
        };
        cables.erase(std::remove_if(cables.begin(), cables.end(), carried), cables.end());
    }
}

void DualIOJackGlide::clearSlides() {
    for (auto& slide : slides_)
        if (slide.part != nullptr)
            slide.part->setTransform({});
    slides_.clear();
}

// A follower-to-follower edge (both ends one jack's second leg) is not drawn at rest; while a glide carries it, it is,
// so a merge's right-leg cable slides into the merged jack and a split's comes out of it instead of popping.
bool DualIOJackGlide::carriesEdge(const ModuleComponent* src, const ModuleComponent* dst,
                                  const juce::AudioProcessorGraph::Connection& c) {
    const auto* in = dst != nullptr ? dst->getDualIOGlide() : nullptr;
    const auto* out = src != nullptr ? src->getDualIOGlide() : nullptr;
    return (in != nullptr && in->carriesCable(c.source.nodeID.uid, c.source.channelIndex, true)) ||
           (out != nullptr && out->carriesCable(c.destination.nodeID.uid, c.destination.channelIndex, false));
}
