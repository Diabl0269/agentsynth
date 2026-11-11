// MacroGroupControllerFold.cpp
//
// Turns a macro's collapse or expand into MacroFoldAnimator plans (docs/layout/animation.md "Macro fold"). The model
// change has already landed by the time a plan is built, so this works in two halves: captureBefore() takes the
// pictures, rects and border the canvas shows BEFORE the change, the builders below pair them with what it shows AFTER
// (the preview boxes on the closed card, or the modules' final cards). An undo or redo of a toggle takes the same two
// halves around its restore, so a fold animates both ways.
//
// A macro nested in a folding one is ONE unit of its parent's plan (its box on the parent's card). When it is open it
// also gets a plan of its own: folding, it runs first (its modules fly into the card it would fold into) and the
// parent's plan waits for it; unfolding, it waits until its box has flown out of the parent's card. A nested macro
// that is folded simply flies as its card. Only the outermost changed macro starts a tree; its nested macros keep their
// own collapsed flags, so they come back exactly as they were left.

#include "MacroGroupController.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"

#include <algorithm>
#include <map>

namespace {
using Fold = MacroFoldAnimator;
using Controller = MacroGroupController;
using Preview = Controller::MacroMemberPreview;

// Past this many modules an unfolding macro whose cards were never painted draws plain boxes rather than rendering
// every card for the pictures.
constexpr size_t kMaxRenderedPictures = 16;

// Each preview's box on `card` standing at `origin` (canvas coordinates): the card's own layout, shifted there. Only
// the card's size shapes its preview area, so this also places the boxes of a card a nested macro has not folded into.
std::vector<juce::Rectangle<float>> boxesOnCard(MacroCardComponent& card, juce::Point<int> origin,
                                                const std::vector<Preview>& previews) {
    std::vector<juce::Rectangle<int>> bounds;
    bounds.reserve(previews.size());
    for (const auto& p : previews)
        bounds.push_back(p.bounds);
    auto boxes = macro_preview::boxes(bounds, card.getPreviewArea());
    for (auto& box : boxes)
        box.translate(static_cast<float>(origin.x), static_cast<float>(origin.y));
    return boxes;
}

bool sameUnit(const Fold::Module& m, const Preview& p) {
    return p.isMacro() ? m.macroId == p.macroId : m.macroId.isEmpty() && m.nodeUid == p.nodeUid;
}

int indexOfUnit(const std::vector<Preview>& previews, const Fold::Module& m) {
    for (size_t i = 0; i < previews.size(); ++i)
        if (sameUnit(m, previews[i]))
            return static_cast<int>(i);
    return -1;
}

// The card an open nested macro folds into: where its own collapse would put it, at its card's size.
juce::Rectangle<int> nestedCardRect(Controller& controller, const juce::String& macroId) {
    auto rect = controller.foldedCardBounds(macroId);
    if (auto* card = controller.getMacroCard(macroId))
        rect.setSize(card->getWidth(), card->getHeight());
    return rect;
}

// A nested macro's picture: its card's raster, or one render of that one card (it is hidden while the macro is open).
juce::Image cardPicture(MacroCardComponent* card) {
    if (card == nullptr || card->getLocalBounds().isEmpty())
        return {};
    auto picture = Fold::cachedPicture(*card);
    return picture.isNull() ? card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f) : picture;
}

Fold::Module macroUnit(const Preview& p) {
    Fold::Module m;
    m.macroId = p.macroId;
    m.colour = p.colour;
    m.rect = p.bounds;
    return m;
}

// A module card as it is drawn right now, which is where a fold of it starts: a fold still flying it (a second Cmd+Z
// mid-fold) or a glide still moving it draws it away from its place. Its picture is the card's own raster, or the one
// the running fold flies.
Fold::Module drawnCard(const CardGlideAnimator& glide, juce::Component& comp, uint32_t nodeUid) {
    if (auto flying = glide.fold().drawnModule(nodeUid)) {
        if (flying->picture.isNull())
            flying->picture = Fold::cachedPicture(comp);
        flying->comp = nullptr;
        return *flying;
    }
    Fold::Module m;
    m.nodeUid = nodeUid;
    const auto gliding = glide.currentRectFor(&comp);
    m.rect = gliding.isEmpty() ? comp.getBounds() : gliding;
    m.picture = Fold::cachedPicture(comp);
    return m;
}

// The half taken before the change, as the canvas draws it (drawnCard). An open macro keeps its modules' rects and
// pictures (the cards' own rasters, never a fresh render) and its border, and each nested macro's card; a closed one
// keeps its card and the box each unit has on it, or where a fold still flying the unit draws it.
Fold::Before captureBefore(Controller& controller, const CardGlideAnimator& glide, const synth::MacroSet& macros,
                           const juce::String& macroId) {
    Fold::Before before;
    const auto* macro = macros.find(macroId);
    if (macro == nullptr)
        return before;
    before.wasCollapsed = macro->collapsed;
    const auto previews = controller.macroMemberPreviews(macroId);

    if (macro->collapsed) {
        auto* card = controller.getMacroCard(macroId);
        if (card == nullptr)
            return before;
        before.card = card->getBounds();
        // A card the glide still moves is drawn on its way: an unfold starts there (Plan::startShift).
        if (const auto gliding = glide.currentRectFor(card); !gliding.isEmpty())
            before.cardOffset = gliding.getPosition() - card->getPosition();
        const auto boxes = boxesOnCard(*card, card->getPosition(), previews);
        for (size_t i = 0; i < previews.size() && i < boxes.size(); ++i) {
            Fold::Module m = previews[i].isMacro() ? macroUnit(previews[i]) : Fold::Module{};
            m.nodeUid = previews[i].nodeUid;
            m.category = previews[i].category;
            m.rect = previews[i].bounds;
            m.box = boxes[i];
            if (const auto flying =
                    m.nodeUid != 0 ? glide.fold().moduleRectFor(m.nodeUid) : glide.fold().nestedRectFor(m.macroId);
                !flying.isEmpty())
                m.box = flying - before.cardOffset.toFloat(); // the start shift puts it back where it is drawn
            before.modules.push_back(std::move(m));
        }
        return before;
    }

    before.hull = controller.macroHullBounds(macroId);
    if (const auto growing = glide.fold().expandOutlineFor(macroId); growing.has_value() && !growing->isEmpty())
        before.hull = *growing;
    for (const auto& p : previews) {
        if (p.isMacro()) {
            const auto* child = macros.find(p.macroId);
            auto m = macroUnit(p);
            if (child != nullptr && !child->collapsed)
                m.rect = nestedCardRect(controller, p.macroId); // its own plan folds it into this card first
            else if (auto* card = controller.getMacroCard(p.macroId))
                m.picture = Fold::cachedPicture(*card);
            before.modules.push_back(std::move(m));
            continue;
        }
        if (p.comp == nullptr || (!p.comp->isVisible() && glide.fold().moduleRectFor(p.nodeUid).isEmpty()))
            continue;
        auto m = drawnCard(glide, *p.comp, p.nodeUid);
        m.category = p.category;
        before.modules.push_back(std::move(m));
    }
    return before;
}

} // namespace

struct MacroGroupController::FoldSnapshot {
    std::map<juce::String, Fold::Before> byMacro;
    std::vector<Fold::VisibleCable> cables; // as drawn before the change
    // Every module card as drawn before the change (rect and picture): a macro created by the change has no entry in
    // byMacro, and its fold starts from these.
    std::map<uint32_t, Fold::Module> nodes;
};

namespace {

// Builds the plans of one changed macro and every open macro nested in it, parent first.
class FoldTreeBuilder {
public:
    FoldTreeBuilder(Controller& controller, GraphCanvasHost& host, const Controller::FoldSnapshot& snapshot)
        : controller_(controller)
        , host_(host)
        , snapshot_(snapshot) {}

    std::vector<Fold::Plan> collapse(const juce::String& macroId, const Fold::Before& before,
                                     const std::vector<Fold::VisibleCable>& cablesAfter) {
        auto* card = controller_.getMacroCard(macroId);
        timeScale_ = 1.0 / openLevels(macroId);
        if (card != nullptr)
            collapsePlan(macroId, before, card->getBounds(), card, 0);
        assignCables(snapshot_.cables, /*bothEnds=*/true, &cablesAfter);
        addSlides(snapshot_.cables, cablesAfter);
        return std::move(plans_);
    }

    std::vector<Fold::Plan> expand(const juce::String& macroId, const Fold::Before& before,
                                   const std::vector<Fold::VisibleCable>& cables) {
        timeScale_ = 1.0 / openLevels(macroId);
        expandPlan(macroId, before.card, &before.modules, 0.0, 0);
        for (auto& plan : plans_)
            if (plan.macroId == macroId)
                plan.startShift = before.cardOffset.toFloat();
        assignCables(cables, /*bothEnds=*/false);
        addSlides(snapshot_.cables, cables);
        return std::move(plans_);
    }

private:
    // The levels of the nest that fold: this macro and the open macros nested in it, to any depth. They share one
    // total (Timeline::scale), so a deep nest folds as fast as a flat macro.
    int openLevels(const juce::String& macroId) const {
        int deepest = 0;
        for (const auto& p : controller_.macroMemberPreviews(macroId)) {
            const auto* child = p.isMacro() ? host_.getMacros().find(p.macroId) : nullptr;
            if (child != nullptr && !child->collapsed)
                deepest = std::max(deepest, openLevels(p.macroId));
        }
        return deepest + 1;
    }

    const Fold::Before* beforeOf(const juce::String& macroId) const {
        const auto it = snapshot_.byMacro.find(macroId);
        return it == snapshot_.byMacro.end() ? nullptr : &it->second;
    }

    // Returns when this macro's last unit has landed on its card, so its parent starts then.
    double collapsePlan(const juce::String& macroId, const Fold::Before& before, juce::Rectangle<int> cardRect,
                        MacroCardComponent* cardComp, int depth) {
        const auto* macro = host_.getMacros().find(macroId);
        auto* card = controller_.getMacroCard(macroId);
        if (macro == nullptr || card == nullptr)
            return 0.0;
        const size_t index = plans_.size();
        plans_.emplace_back();
        {
            auto& plan = plans_[index];
            plan.macroId = macroId;
            plan.collapsing = true;
            plan.colour = macro->colour;
            plan.hull = before.hull;
            plan.card = cardRect;
            plan.cardComp = cardComp;
            plan.timeScale = timeScale_;
        }
        const auto previews = controller_.macroMemberPreviews(macroId);
        const auto boxes = boxesOnCard(*card, cardRect.getPosition(), previews);
        std::vector<Fold::Module> modules;
        double delay = 0.0;
        for (const auto& m : before.modules) {
            const int i = indexOfUnit(previews, m);
            if (i < 0 || static_cast<size_t>(i) >= boxes.size())
                continue;
            auto unit = m;
            unit.box = boxes[static_cast<size_t>(i)];
            if (unit.macroId.isNotEmpty()) {
                const auto* nested = beforeOf(unit.macroId);
                if (nested != nullptr && !nested->wasCollapsed && !nested->modules.empty())
                    delay = std::max(delay, collapsePlan(unit.macroId, *nested, unit.rect, nullptr, depth + 1));
                if (unit.picture.isNull())
                    unit.picture = cardPicture(controller_.getMacroCard(unit.macroId));
            }
            note(unit, index, depth);
            modules.push_back(std::move(unit));
        }
        auto& plan = plans_[index];
        plan.modules = std::move(modules);
        plan.delayMs = delay;
        return delay + synth::ui::macro_fold::Timeline{static_cast<int>(plan.modules.size()), true, false, timeScale_}
                           .modulesEndMs();
    }

    // `beforeUnits`: the boxes the units leave from (the closed card captured before the change); null for a nested
    // macro, whose boxes are laid out on the card it unfolds from.
    void expandPlan(const juce::String& macroId, juce::Rectangle<int> cardRect,
                    const std::vector<Fold::Module>* beforeUnits, double delay, int depth) {
        const auto* macro = host_.getMacros().find(macroId);
        auto* card = controller_.getMacroCard(macroId);
        if (macro == nullptr || card == nullptr)
            return;
        const auto previews = controller_.macroMemberPreviews(macroId);
        const auto nestedBoxes = beforeUnits == nullptr ? boxesOnCard(*card, cardRect.getPosition(), previews)
                                                        : std::vector<juce::Rectangle<float>>();
        const size_t index = plans_.size();
        plans_.emplace_back();
        std::vector<Fold::Module> modules;
        std::vector<juce::String> nestedOpen;
        for (size_t i = 0; i < previews.size(); ++i) {
            const auto& p = previews[i];
            Fold::Module out = p.isMacro() ? macroUnit(p) : Fold::Module{};
            if (beforeUnits != nullptr) {
                const auto it = std::find_if(beforeUnits->begin(), beforeUnits->end(),
                                             [&p](const Fold::Module& m) { return sameUnit(m, p); });
                if (it == beforeUnits->end())
                    continue;
                out.box = it->box;
            } else if (i < nestedBoxes.size()) {
                out.box = nestedBoxes[i];
            } else {
                continue;
            }
            if (p.isMacro()) {
                const auto* child = host_.getMacros().find(p.macroId);
                auto* childCard = controller_.getMacroCard(p.macroId);
                if (child == nullptr || childCard == nullptr)
                    continue;
                out.picture = cardPicture(childCard);
                if (child->collapsed) {
                    out.comp = childCard; // the real card, held until its box lands
                } else {
                    out.rect = nestedCardRect(controller_, p.macroId);
                    nestedOpen.push_back(p.macroId);
                }
            } else {
                if (p.comp == nullptr || !p.comp->isVisible())
                    continue;
                out.nodeUid = p.nodeUid;
                out.category = p.category;
                out.rect = p.bounds;
                out.comp = p.comp;
                out.picture = Fold::cachedPicture(*p.comp);
                // Only a card that will be seen gets a picture rendered: unfolding every macro rendered every member.
                if (out.picture.isNull() && previews.size() <= kMaxRenderedPictures &&
                    p.bounds.intersects(card_glide_detail::visibleCanvasArea(*p.comp)))
                    out.picture = p.comp->createComponentSnapshot(p.comp->getLocalBounds(), true, 1.0f);
            }
            note(out, index, depth);
            modules.push_back(std::move(out));
        }
        {
            auto& plan = plans_[index];
            plan.macroId = macroId;
            plan.collapsing = false;
            plan.colour = macro->colour;
            plan.hull = controller_.macroHullBounds(macroId);
            plan.card = cardRect;
            plan.delayMs = delay;
            plan.timeScale = timeScale_;
            plan.modules = std::move(modules);
            for (const auto& port : macro->ports)
                if (auto* widget = host_.moduleComponentFor(controller_.resolveMemberNodeId(port.nodeUuid)))
                    plan.ports.push_back(widget);
        }
        // Each open nested macro unfolds once its box has landed on the card it unfolds from.
        const synth::ui::macro_fold::Timeline timeline{static_cast<int>(plans_[index].modules.size()), false, false,
                                                       timeScale_};
        for (const auto& nestedId : nestedOpen) {
            const auto& units = plans_[index].modules;
            const auto it = std::find_if(units.begin(), units.end(),
                                         [&nestedId](const Fold::Module& m) { return m.macroId == nestedId; });
            const auto unitIndex = static_cast<int>(std::distance(units.begin(), it));
            expandPlan(nestedId, it->rect, nullptr, delay + timeline.landMs(unitIndex), depth + 1);
        }
    }

    void note(const Fold::Module& m, size_t planIndex, int depth) {
        if (m.nodeUid != 0)
            ownerOf_[m.nodeUid] = {planIndex, depth};
    }

    // A cable goes to the plan of its deeper end, so a cable from a nested macro's module to its parent's fades (or
    // draws out) with the nested macro's own cables. Folding, only cables with both ends flying are kept (one that
    // leaves the macro re-attaches to the card at once); unfolding, every cable touching a module is held for it.
    void assignCables(const std::vector<Fold::VisibleCable>& cables, bool bothEnds,
                      const std::vector<Fold::VisibleCable>* after = nullptr) {
        for (const auto& c : cables) {
            const auto src = ownerOf_.find(c.id.srcUid), dst = ownerOf_.find(c.id.dstUid);
            const bool hasSrc = src != ownerOf_.end(), hasDst = dst != ownerOf_.end();
            // Folding, a cable from a flying module to the border's port goes with the fold too: it rides its module
            // and fades, since the closed card no longer draws it.
            const bool goneWithFold = after != nullptr && (hasSrc || hasDst) && !contains(*after, c.id);
            if (bothEnds ? !(hasSrc && hasDst) && !goneWithFold : !(hasSrc || hasDst))
                continue;
            const auto owner =
                !hasDst || (hasSrc && src->second.second >= dst->second.second) ? src->second : dst->second;
            plans_[owner.first].cables.push_back(c);
        }
    }

    static bool contains(const std::vector<Fold::VisibleCable>& cables, const graph_editor_types::CableId& id) {
        return std::any_of(cables.begin(), cables.end(), [&id](const Fold::VisibleCable& c) { return c.id == id; });
    }

    // A cable drawn before and after whose end on this macro moved (the closed card's jack <-> the open border's port)
    // slides with the border instead of jumping. Only an end near the macro slides; cables touching a flying module
    // already belong to a plan.
    void addSlides(const std::vector<Fold::VisibleCable>& before, const std::vector<Fold::VisibleCable>& after) {
        if (plans_.empty())
            return;
        auto& root = plans_.front();
        const auto area = root.hull.getUnion(root.card).expanded(kSlideReach).toFloat();
        const auto moved = [](juce::Point<float> a, juce::Point<float> b) { return a.getDistanceFrom(b) > 0.5f; };
        for (const auto& now : after) {
            if (ownerOf_.count(now.id.srcUid) != 0 || ownerOf_.count(now.id.dstUid) != 0)
                continue;
            const auto was = std::find_if(before.begin(), before.end(),
                                          [&now](const Fold::VisibleCable& c) { return c.id == now.id; });
            if (was == before.end())
                continue;
            Fold::CableSlide slide{now, now.p1, now.p2};
            if (moved(was->p1, now.p1) && (area.contains(was->p1) || area.contains(now.p1)))
                slide.from1 = was->p1;
            if (moved(was->p2, now.p2) && (area.contains(was->p2) || area.contains(now.p2)))
                slide.from2 = was->p2;
            if (slide.from1 != now.p1 || slide.from2 != now.p2)
                root.slides.push_back(slide);
        }
    }

    static constexpr int kSlideReach = 24; // a jack or port sits on the card's or border's edge, a little outside it

    Controller& controller_;
    GraphCanvasHost& host_;
    const Controller::FoldSnapshot& snapshot_;
    std::vector<Fold::Plan> plans_;
    double timeScale_ = 1.0;
    std::map<uint32_t, std::pair<size_t, int>> ownerOf_; // node uid -> (plan, nesting depth)
};

// Starts the folds, and lets the folded macros' modules keep their own cards out of the delete and undo ghosts: an
// undo of a collapse would otherwise also shrink (or grow) every one of them.
// `goneMacros` are macros the change removed: the fold draws their closed card itself, so their card must not also
// shrink away as an exit ghost (that phase would hold the neighbours' glide back until it ended).
void startFolds(CardGlideAnimator& glide, std::vector<Fold::Plan> plans, const std::vector<juce::String>& goneMacros) {
    std::vector<uint32_t> uids;
    for (const auto& id : goneMacros) {
        uids.push_back(CardGlideAnimator::macroKey(id));
        uids.push_back(CardGlideAnimator::borderKey(CardGlideAnimator::macroKey(id)));
    }
    for (const auto& plan : plans) {
        // An opening macro's closed card is gone from the canvas: the fold grows its border out of where it was drawn,
        // so a glide still moving the card's picture would draw it twice.
        if (!plan.collapsing)
            uids.push_back(CardGlideAnimator::macroKey(plan.macroId));
        for (const auto& m : plan.modules)
            if (m.nodeUid != 0)
                uids.push_back(m.nodeUid);
    }
    if (glide.fold().arm(std::move(plans)))
        glide.dropGhostsFor(uids);
}

} // namespace

std::shared_ptr<const MacroGroupController::FoldSnapshot> MacroGroupController::snapshotFoldState() {
    auto& glide = host_.cardGlide();
    auto& fold = glide.fold();
    if (!fold.wouldAnimate())
        return nullptr;
    auto out = std::make_shared<FoldSnapshot>();
    out->cables = fold.currentCables();
    for (const auto& macro : host_.getMacros().getAll())
        if (host_.getMacros().isVisible(macro.id))
            out->byMacro[macro.id] = captureBefore(*this, glide, host_.getMacros(), macro.id);
    for (auto* comp : host_.modules()) {
        if (comp == nullptr)
            continue;
        const auto uid = comp->getNodeId().uid;
        if (!comp->isVisible() && fold.moduleRectFor(uid).isEmpty())
            continue;
        out->nodes[uid] = drawnCard(glide, *comp, uid);
    }
    return out;
}

// The expand plan of a closed macro that no longer exists: each module that stands visible again flies from its box on
// the card (captured before the change) to its card. Nested macros and modules that are gone are left out.
namespace {
Fold::Plan unfoldRemovedMacro(GraphCanvasHost& host, const juce::String& macroId, const Fold::Before& before) {
    Fold::Plan plan;
    plan.macroId = macroId;
    plan.collapsing = false;
    plan.card = before.card;
    plan.timeScale = 1.0;
    juce::Rectangle<int> hull;
    for (const auto& unit : before.modules) {
        if (unit.nodeUid == 0)
            continue;
        auto* comp = host.moduleComponentFor(juce::AudioProcessorGraph::NodeID(unit.nodeUid));
        if (comp == nullptr || !comp->isVisible())
            continue;
        Fold::Module out;
        out.nodeUid = unit.nodeUid;
        out.category = unit.category;
        out.box = unit.box;
        out.rect = comp->getBounds();
        out.comp = comp;
        out.picture = Fold::cachedPicture(*comp);
        if (out.picture.isNull() && before.modules.size() <= kMaxRenderedPictures &&
            out.rect.intersects(card_glide_detail::visibleCanvasArea(*comp)))
            out.picture = comp->createComponentSnapshot(comp->getLocalBounds(), true, 1.0f);
        hull = hull.isEmpty() ? out.rect : hull.getUnion(out.rect);
        plan.modules.push_back(std::move(out));
    }
    plan.hull = hull;
    return plan;
}
} // namespace

// Only the outermost macro whose state changed starts a fold: the macros nested in it fold inside its plans.
void MacroGroupController::foldChangedMacros(const std::shared_ptr<const FoldSnapshot>& before) {
    if (before == nullptr)
        return;
    const auto& macros = host_.getMacros();
    const auto changed = [&](const juce::String& id) {
        const auto* macro = macros.find(id);
        const auto it = before->byMacro.find(id);
        return macro != nullptr && it != before->byMacro.end() && macro->collapsed != it->second.wasCollapsed;
    };
    std::vector<Fold::Plan> plans;
    std::vector<juce::String> goneMacros;
    std::vector<Fold::VisibleCable> cablesAfter;
    bool haveCablesAfter = false;
    // A macro the change created (grouping, or the redo of it) folds like a collapse of the same group: it was open, as
    // its members stood on the canvas, and is now closed.
    std::map<juce::String, Fold::Before> created;
    for (const auto& macro : macros.getAll()) {
        if (before->byMacro.count(macro.id) != 0 || !macro.collapsed || !macros.isVisible(macro.id))
            continue;
        auto& entry = created[macro.id];
        entry.hull = macroOpenHullBounds(macro.id);
        for (const auto& p : macroMemberPreviews(macro.id)) {
            if (p.isMacro()) {
                const auto it = before->byMacro.find(p.macroId);
                if (it == before->byMacro.end())
                    continue;
                auto m = macroUnit(p);
                if (!it->second.wasCollapsed)
                    m.rect = nestedCardRect(*this, p.macroId);
                else if (auto* card = getMacroCard(p.macroId))
                    m.picture = Fold::cachedPicture(*card);
                entry.modules.push_back(std::move(m));
                continue;
            }
            const auto it = before->nodes.find(p.nodeUid);
            if (it == before->nodes.end())
                continue;
            auto m = it->second;
            m.category = p.category;
            entry.modules.push_back(std::move(m));
        }
    }
    const auto isCreated = [&](const juce::String& id) { return created.count(id) != 0; };

    for (const auto& [id, snapshot] : before->byMacro) {
        if (!changed(id) || snapshot.modules.empty())
            continue;
        const auto ancestors = macros.ancestorChain(id);
        if (std::any_of(ancestors.begin(), ancestors.end(), changed))
            continue;
        FoldTreeBuilder builder(*this, host_, *before);
        std::vector<Fold::Plan> tree;
        if (!haveCablesAfter)
            cablesAfter = host_.cardGlide().fold().currentCables();
        haveCablesAfter = true;
        const bool collapsed = macros.find(id)->collapsed;
        if (collapsed)
            tree = builder.collapse(id, snapshot, cablesAfter);
        else
            tree = builder.expand(id, snapshot, cablesAfter);
        for (auto& plan : tree) {
            plan.rootId = id;
            plan.rootCollapsed = collapsed;
            plans.push_back(std::move(plan));
        }
    }
    for (const auto& [id, snapshot] : created) {
        if (snapshot.modules.empty())
            continue;
        const auto ancestors = macros.ancestorChain(id);
        if (std::any_of(ancestors.begin(), ancestors.end(),
                        [&](const juce::String& a) { return changed(a) || isCreated(a); }))
            continue;
        FoldTreeBuilder builder(*this, host_, *before);
        if (!haveCablesAfter)
            cablesAfter = host_.cardGlide().fold().currentCables();
        haveCablesAfter = true;
        for (auto& plan : builder.collapse(id, snapshot, cablesAfter)) {
            plan.rootId = id;
            plans.push_back(std::move(plan));
        }
    }
    // A collapsed macro the change removed (undo of a grouping, or Ungroup) whose modules stand on the canvas again:
    // they unfold out of the box they had on the card it leaves.
    for (const auto& [id, snapshot] : before->byMacro) {
        if (macros.find(id) != nullptr || !snapshot.wasCollapsed || snapshot.modules.empty())
            continue;
        if (auto plan = unfoldRemovedMacro(host_, id, snapshot); !plan.modules.empty()) {
            plan.rootId = id;
            plan.rootRemoved = true;
            plans.push_back(std::move(plan));
            goneMacros.push_back(id);
        }
    }
    if (!plans.empty())
        startFolds(host_.cardGlide(), std::move(plans), goneMacros);
}
