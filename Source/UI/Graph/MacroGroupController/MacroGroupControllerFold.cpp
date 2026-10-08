// MacroGroupControllerFold.cpp
//
// Turns a macro's collapse or expand into a MacroFoldAnimator plan (docs/layout/animation.md "Macro fold"). The model
// change has already landed by the time a plan is built, so this works in two halves: captureFoldBefore() takes the
// pictures, rects and border the canvas shows BEFORE the change, buildFoldPlan() pairs them with what it shows AFTER
// (the preview boxes on the closed card, or the modules' final cards). An undo or redo of a toggle takes the same two
// halves around its restore, so a fold animates both ways.

#include "MacroGroupController.h"

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include "UI/Macros/MacroCardComponent/MacroPreviewLayout.h"

#include <algorithm>
#include <unordered_set>

namespace {
using Fold = MacroFoldAnimator;

// Past this many modules an unfolding macro whose cards were never painted draws plain boxes rather than rendering
// every card for the pictures.
constexpr size_t kMaxRenderedPictures = 16;

std::vector<juce::Rectangle<int>> boundsOf(const std::vector<MacroGroupController::MacroMemberPreview>& previews) {
    std::vector<juce::Rectangle<int>> out;
    out.reserve(previews.size());
    for (const auto& p : previews)
        out.push_back(p.bounds);
    return out;
}

// Each preview's box on the closed card, in canvas coordinates: the card's own layout, shifted by its position.
std::vector<juce::Rectangle<float>> boxesOnCard(MacroCardComponent& card,
                                                const std::vector<MacroGroupController::MacroMemberPreview>& previews) {
    auto boxes = macro_preview::boxes(boundsOf(previews), card.getPreviewArea());
    const auto origin = card.getPosition().toFloat();
    for (auto& box : boxes)
        box.translate(origin.x, origin.y);
    return boxes;
}

bool touches(const std::unordered_set<uint32_t>& uids, const Fold::VisibleCable& c) {
    return uids.count(c.id.srcUid) != 0 || uids.count(c.id.dstUid) != 0;
}

bool inside(const std::unordered_set<uint32_t>& uids, const Fold::VisibleCable& c) {
    return uids.count(c.id.srcUid) != 0 && uids.count(c.id.dstUid) != 0;
}

using Controller = MacroGroupController;

// The half taken before the change. An open macro keeps its modules' rects and pictures (the cards' own rasters, never
// a fresh render), the border and the cables between its modules; a closed one keeps its card and the box each module
// has on it.
Fold::Before captureBefore(Controller& controller, const synth::MacroSet& macros, const juce::String& macroId,
                           const std::vector<Fold::VisibleCable>& cables) {
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
        const auto boxes = boxesOnCard(*card, previews);
        for (size_t i = 0; i < previews.size() && i < boxes.size(); ++i) {
            Fold::Module m;
            m.nodeUid = previews[i].nodeUid;
            m.category = previews[i].category;
            m.rect = previews[i].bounds;
            m.box = boxes[i];
            before.modules.push_back(std::move(m));
        }
        return before;
    }

    before.hull = controller.macroHullBounds(macroId);
    std::unordered_set<uint32_t> uids;
    for (const auto& p : previews) {
        if (p.comp == nullptr || !p.comp->isVisible())
            continue; // inside a collapsed child: its card stands in
        Fold::Module m;
        m.nodeUid = p.nodeUid;
        m.category = p.category;
        m.rect = p.bounds;
        m.picture = Fold::cachedPicture(*p.comp);
        before.modules.push_back(std::move(m));
        uids.insert(p.nodeUid);
    }
    for (const auto& c : cables)
        if (inside(uids, c))
            before.cables.push_back(c);
    return before;
}

// The half taken after: nothing when the macro did not change state (or has no module to fly).
std::optional<Fold::Plan> buildPlan(Controller& controller, GraphCanvasHost& host, MacroFoldAnimator& fold,
                                    const juce::String& macroId, const Fold::Before& before) {
    const auto* macro = host.getMacros().find(macroId);
    if (macro == nullptr || macro->collapsed == before.wasCollapsed || before.modules.empty())
        return std::nullopt;
    auto* card = controller.getMacroCard(macroId);
    if (card == nullptr)
        return std::nullopt;

    Fold::Plan plan;
    plan.macroId = macroId;
    plan.collapsing = macro->collapsed;
    plan.colour = macro->colour;
    const auto previews = controller.macroMemberPreviews(macroId);
    const auto indexOfUid = [&previews](uint32_t uid) -> int {
        for (size_t i = 0; i < previews.size(); ++i)
            if (previews[i].nodeUid == uid)
                return static_cast<int>(i);
        return -1;
    };

    if (plan.collapsing) {
        plan.hull = before.hull;
        plan.card = card->getBounds();
        plan.cardComp = card;
        plan.cables = before.cables;
        const auto boxes = boxesOnCard(*card, previews);
        for (const auto& m : before.modules)
            if (const int i = indexOfUid(m.nodeUid); i >= 0 && static_cast<size_t>(i) < boxes.size()) {
                plan.modules.push_back(m);
                plan.modules.back().box = boxes[static_cast<size_t>(i)];
            }
        return plan;
    }

    plan.hull = controller.macroHullBounds(macroId);
    plan.card = before.card;
    std::unordered_set<uint32_t> uids;
    for (const auto& m : before.modules) {
        const int i = indexOfUid(m.nodeUid);
        if (i < 0)
            continue;
        auto* comp = previews[static_cast<size_t>(i)].comp;
        if (comp == nullptr || !comp->isVisible())
            continue;
        Fold::Module out = m;
        out.rect = previews[static_cast<size_t>(i)].bounds;
        out.comp = comp;
        out.picture = Fold::cachedPicture(*comp);
        if (out.picture.isNull() && before.modules.size() <= kMaxRenderedPictures)
            out.picture = comp->createComponentSnapshot(comp->getLocalBounds(), true, 1.0f);
        plan.modules.push_back(std::move(out));
        uids.insert(m.nodeUid);
    }
    for (auto& c : fold.currentCables())
        if (touches(uids, c))
            plan.cables.push_back(std::move(c));
    for (const auto& port : macro->ports)
        if (auto* widget = host.moduleComponentFor(controller.resolveMemberNodeId(port.nodeUuid)))
            plan.ports.push_back(widget);
    return plan;
}
// Starts the folds, and lets the folded macros' modules keep their own cards out of the delete and undo ghosts: an
// undo of a collapse would otherwise also shrink (or grow) every one of them.
void startFolds(CardGlideAnimator& glide, std::vector<Fold::Plan> plans) {
    std::vector<uint32_t> uids;
    for (const auto& plan : plans)
        for (const auto& m : plan.modules)
            uids.push_back(m.nodeUid);
    if (glide.fold().arm(std::move(plans)))
        glide.dropGhostsFor(uids);
}

} // namespace

struct MacroGroupController::FoldSnapshot {
    std::map<juce::String, Fold::Before> byMacro;
};

std::shared_ptr<const MacroGroupController::FoldSnapshot> MacroGroupController::snapshotFoldState() {
    auto& fold = host_.cardGlide().fold();
    if (!fold.wouldAnimate())
        return nullptr;
    auto out = std::make_shared<FoldSnapshot>();
    const auto cables = fold.currentCables();
    for (const auto& macro : host_.getMacros().getAll())
        if (host_.getMacros().isVisible(macro.id))
            out->byMacro[macro.id] = captureBefore(*this, host_.getMacros(), macro.id, cables);
    return out;
}

void MacroGroupController::foldChangedMacros(const std::shared_ptr<const FoldSnapshot>& before) {
    if (before == nullptr)
        return;
    std::vector<Fold::Plan> plans;
    for (const auto& [id, snapshot] : before->byMacro)
        if (auto plan = buildPlan(*this, host_, host_.cardGlide().fold(), id, snapshot))
            plans.push_back(std::move(*plan));
    if (!plans.empty())
        startFolds(host_.cardGlide(), std::move(plans));
}
