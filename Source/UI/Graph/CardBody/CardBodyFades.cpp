// CardBodyFades.cpp -- the parts of a card body that appear and disappear in place and fade while they do
// (docs/layout/animation.md#fading-things-in-and-out): a view the card's toggle opens or closes, the More row's
// controls, and a conditional section that comes or goes with a mode. Each is a CardBlockFade (the shared
// FadeVisibility plus a height that follows its progress); the card's height follows every frame, and the
// neighbours make room once per direction. A swap of controls in place keeps its own motion
// (CardBodySwapMotion.cpp); only a show or hide that is not a swap fades here.
#include "CardBody.h"
#include "CardBodyViews.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"

namespace synth {

void CardBody::attachViewFade(int item) {
    auto& planned = plan_.items[(size_t)item];
    if (planned.widget == nullptr)
        return;
    if (viewFades_.size() < plan_.items.size())
        viewFades_.resize(plan_.items.size());
    viewFades_[(size_t)item].attach({planned.widget}, [this] { relayoutForFade(); }, [this] { settleAfterFade(); });
}

// One frame of any fade of this body: the card measures itself again (refreshReveals reads every fade's progress).
void CardBody::relayoutForFade() { card_.updateLayout(); }

// A fade-out has ended: the card is as small as it will be, so its neighbours get their room back.
void CardBody::settleAfterFade() {
    card_.updateLayout();
    card_.owner.handleModuleResized(&card_);
}

// What a view takes of its height while it fades. A view that is not fading follows `open` (-1), which keeps a
// view in a tab that is not selected (hidden, but measured) the height it always had.
void CardBody::refreshReveals() const {
    for (size_t i = 0; i < viewFades_.size() && i < plan_.items.size(); ++i)
        plan_.items[i].reveal = viewFades_[i].isFading() ? viewFades_[i].fraction(settlingFootprint_) : -1.0f;
}

bool CardBody::isFadeRunning() const {
    if (moreFade_.isFading())
        return true;
    for (const auto* fades : {&viewFades_, &sectionFades_})
        for (const auto& fade : *fades)
            if (fade.isFading())
                return true;
    return false;
}

const CardBlockFade& CardBody::viewFadeForTest(CardView view) const {
    static const CardBlockFade none;
    for (size_t i = 0; i < plan_.items.size() && i < viewFades_.size(); ++i)
        if (plan_.items[i].kind == CardBodyItem::Kind::View && plan_.items[i].view == view)
            return viewFades_[i];
    return none;
}

// While a fade runs it owns the visibility of its controls; applyVisibility leaves them alone.
bool CardBody::fadeOwnsMore() const { return moreFade_.isAttached() && (moreFade_.isFading() || startingMore_); }

bool CardBody::fadeOwnsSection(int section) const {
    return section >= 0 && (size_t)section < sectionFades_.size() && sectionFades_[(size_t)section].isAttached() &&
           (sectionFades_[(size_t)section].isFading() || startingSections_.count(section) > 0);
}

// A conditional section fades when it is a plain grid of single controls: no tab or look (those swap in place),
// no swap group or second placement (a swap), no view (its own fade).
bool CardBody::isFadeableSection(int index) const {
    const auto& section = plan_.sections[(size_t)index];
    if (!section.visibleWhen.has_value() || section.footer || section.tabGroup >= 0 || section.altGroup >= 0 ||
        section.items.empty())
        return false;
    for (int i : section.items) {
        const auto& item = plan_.items[(size_t)i];
        if (item.widget == nullptr || item.kind == CardBodyItem::Kind::View || item.swapGroup >= 0 ||
            !item.alsoIn.empty())
            return false;
    }
    return true;
}

std::vector<juce::Component*> CardBody::sectionTargets(int index) const {
    const auto& section = plan_.sections[(size_t)index];
    std::vector<juce::Component*> targets;
    if (section.header != nullptr)
        targets.push_back(section.header);
    for (int i : section.items)
        for (auto* component : {plan_.items[(size_t)i].widget, plan_.items[(size_t)i].label})
            if (component != nullptr)
                targets.push_back(component);
    return targets;
}

// After every widget exists. The state each fade starts from is what the components show (applyVisibility sets it).
void CardBody::attachMoreAndSectionFades() {
    std::vector<juce::Component*> moreTargets;
    for (int i : plan_.more)
        for (auto* component : {plan_.items[(size_t)i].widget, plan_.items[(size_t)i].label})
            if (component != nullptr)
                moreTargets.push_back(component);
    if (!moreTargets.empty())
        moreFade_.attach(std::move(moreTargets), [this] { relayoutForFade(); }, [this] { settleAfterFade(); });

    sectionFades_.resize(plan_.sections.size());
    for (int s = 0; s < (int)plan_.sections.size(); ++s)
        if (isFadeableSection(s))
            sectionFades_[(size_t)s].attach(
                sectionTargets(s), {}, // a section keeps its height until it has gone (see beginSectionFades)
                [this, s] {
                    plan_.sections[(size_t)s].holding = false; // gone for good: the card closes up
                    settleAfterFade();
                    if (onConditionsApplied) // the on-card layout editor re-reads its outlines
                        onConditionsApplied();
                });
}

// A condition re-read has flipped some sections. Those that can fade and may animate now are marked: a section going
// away stays in the layout (`holding`) until its controls have faded out, one coming stays hidden until the fade
// starts. Returns false when none of them fades, which leaves everything to applyVisibility.
bool CardBody::beginSectionFades(const std::vector<bool>& wasVisible) {
    if (!ui::FadeVisibility::canAnimateIn(&card_))
        return false;
    for (int s = 0; s < (int)plan_.sections.size() && (size_t)s < wasVisible.size(); ++s) {
        auto& section = plan_.sections[(size_t)s];
        if (section.visible == wasVisible[(size_t)s] || (size_t)s >= sectionFades_.size() ||
            !sectionFades_[(size_t)s].isAttached())
            continue;
        startingSections_.insert(s);
        section.holding = !section.visible;
    }
    return !startingSections_.empty();
}

} // namespace synth
