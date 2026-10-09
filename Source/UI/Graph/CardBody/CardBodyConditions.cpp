// CardBodyConditions.cpp -- a card body's conditions, read live: an item's `when` (show: present only
// while it holds; dim: greyed out unless it holds), a section's `visibleWhen`, and the code dim rules of
// a code default. Choice conditions match value strings, bool ones "true"/"false".
// docs/layout/module-card-layout.md#rendering.
#include "CardBody.h"
#include "CardBodyMoreButton.h"
#include "CardBodyPlan.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <set>

namespace synth {

bool cardConditionHolds(const CardCondition& condition, juce::AudioProcessor& module) {
    auto* param = findParameterByID(&module, condition.param);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param))
        return condition.is.contains(choice->getCurrentChoiceName());
    if (auto* flag = dynamic_cast<juce::AudioParameterBool*>(param))
        return condition.is.contains(flag->get() ? "true" : "false");
    return true;
}

namespace {

bool assign(bool& target, bool value) {
    const bool changed = target != value;
    target = value;
    return changed;
}

constexpr const char* kDimmedHint = "Inactive in this mode";
constexpr const char* kDimmedTooltipSuffix = " (inactive in this mode)";

void markDimmed(juce::Component& component, bool dimmed) {
    if ((bool)component.getProperties()[synth::theme::AppLookAndFeel::kDimmedProperty] == dimmed)
        return;
    component.getProperties().set(synth::theme::AppLookAndFeel::kDimmedProperty, dimmed);
    component.repaint();
}

// A screen reader hears the hint as the control's description, and the tooltip carries it too; both go
// again when the dim lifts. Only the suffix this adds is ever removed, so a tooltip the MIDI Learn
// badge composes keeps the rest of its text.
void describeDimmed(juce::Component& widget, bool dimmed) {
    if (dimmed)
        widget.setDescription(kDimmedHint);
    else if (widget.getDescription() == kDimmedHint)
        widget.setDescription({});
    auto* tip = dynamic_cast<juce::SettableTooltipClient*>(&widget);
    if (tip == nullptr)
        return;
    const auto tooltip = tip->getTooltip();
    const bool hinted = tooltip.endsWith(kDimmedTooltipSuffix);
    if (dimmed && !hinted)
        tip->setTooltip(tooltip + kDimmedTooltipSuffix);
    else if (!dimmed && hinted)
        tip->setTooltip(tooltip.dropLastCharacters((int)juce::String(kDimmedTooltipSuffix).length()));
}

bool ruleDims(const std::vector<CardBodyPlan::DimRule>& rules, int item, juce::AudioProcessor& module) {
    for (const auto& rule : rules)
        if (rule.item == item && rule.dims(module))
            return true;
    return false;
}

} // namespace

CardBodyPlan::ConditionChange CardBodyPlan::evaluateConditions(juce::AudioProcessor& module) {
    ConditionChange change;
    for (auto& section : sections) {
        const bool visible = !section.visibleWhen.has_value() || cardConditionHolds(*section.visibleWhen, module);
        change.sections |= assign(section.visible, visible);
    }
    for (int i = 0; i < (int)items.size(); ++i) {
        auto& item = items[(size_t)i];
        const bool holds = !item.when.has_value() || cardConditionHolds(*item.when, module);
        const bool show = item.when.has_value() && item.when->effect == CardConditionEffect::Show;
        change.any |= assign(item.shown, !show || holds);
        change.any |= assign(item.dimmed, (!show && !holds) || ruleDims(dimRules, i, module));
    }
    change.any |= change.sections;
    return change;
}

std::vector<juce::RangedAudioParameter*> CardBodyPlan::watchedParameters(juce::AudioProcessor& module) const {
    std::vector<juce::RangedAudioParameter*> watched;
    std::set<juce::RangedAudioParameter*> seen;
    auto add = [&](juce::RangedAudioParameter* param) {
        if (param != nullptr && seen.insert(param).second)
            watched.push_back(param);
    };
    auto addId = [&](const juce::String& id) { add(findParameterByID(&module, id)); };
    for (const auto& section : sections)
        if (section.visibleWhen.has_value())
            addId(section.visibleWhen->param);
    for (const auto& item : items)
        if (item.when.has_value())
            addId(item.when->param);
    for (const auto& rule : dimRules)
        for (auto* param : rule.watched)
            add(param);
    return watched;
}

bool CardBodyPlan::isOnCard(int item) const {
    if (item < 0 || item >= (int)items.size())
        return false;
    const auto onCard = [this](int section) {
        if (section < 0 || !(sections[(size_t)section].visible || sections[(size_t)section].holding))
            return false; // a section fading out is still on the card
        const int group = sections[(size_t)section].tabGroup;
        return group < 0 || tabGroups[(size_t)group].sections[(size_t)tabGroups[(size_t)group].selected] == section;
    };
    const auto& planned = items[(size_t)item];
    return onCard(planned.section) ||
           std::any_of(planned.alsoIn.begin(), planned.alsoIn.end(), [&](const auto& p) { return onCard(p.section); });
}

bool CardBodyPlan::isInAltGroup(int item) const {
    if (item < 0 || item >= (int)items.size())
        return false;
    const auto& planned = items[(size_t)item];
    const auto inGroup = [this](int section) { return section >= 0 && sections[(size_t)section].altGroup >= 0; };
    return inGroup(planned.section) ||
           std::any_of(planned.alsoIn.begin(), planned.alsoIn.end(), [&](const auto& p) { return inGroup(p.section); });
}

std::optional<juce::Point<int>> CardBodyPlan::atIn(int item, int section) const {
    const auto& planned = items[(size_t)item];
    if (planned.section == section)
        return planned.at;
    for (const auto& placement : planned.alsoIn)
        if (placement.section == section)
            return placement.at;
    return std::nullopt;
}

bool CardBodyPlan::isTabbed(int item) const {
    if (item < 0 || item >= (int)items.size())
        return false;
    const int section = items[(size_t)item].section;
    return section >= 0 && sections[(size_t)section].tabGroup >= 0;
}

juce::String CardBodyPlan::tabTitle(int section) const {
    const auto& title = sections[(size_t)section].title;
    return title.has_value() && title->trim().isNotEmpty() ? title->trim() : sections[(size_t)section].id;
}

bool CardBodyPlan::hasFooter() const {
    for (const auto& section : sections)
        if (section.footer)
            return true;
    return false;
}

// ---- The live card body ----------------------------------------------------------------------------

// What shows now: a More-row parameter while the row is unfolded; a placed one while its section is
// shown (a tab section only while its tab is selected) and (in a swap group) its condition holds. Only
// those the body governs are touched (the More row, swap groups, conditional and tab sections), so a
// widget a card hides itself stays as the card left it. A folded, swapped-out or other-tab widget is hidden, not
// destroyed: it keeps its attachment, MIDI Learn entry and value. A dimmed widget and its caption are
// marked (AppLookAndFeel::kDimmedProperty), which the look-and-feel paints greyed out; it keeps its
// cell and stays enabled, so it is still a Tab stop, in the accessibility tree and operable.
void CardBody::applyVisibility() {
    const std::set<int> inMore(plan_.more.begin(), plan_.more.end());
    std::set<int> dimmable;
    for (const auto& rule : plan_.dimRules)
        dimmable.insert(rule.item);
    for (int i = 0; i < (int)plan_.items.size(); ++i) {
        auto& item = plan_.items[(size_t)i];
        const bool folded = inMore.count(i) > 0;
        const bool governed = folded || item.swapGroup >= 0 || plan_.isTabbed(i) ||
                              (item.section >= 0 && plan_.sections[(size_t)item.section].visibleWhen.has_value());
        bool visible = folded ? moreUnfolded_ : plan_.isOnCard(i) && item.shown;
        if (item.kind == CardBodyItem::Kind::View)
            visible = visible && item.open; // a view the card's toggle closed stays closed
        const bool dims = item.when.has_value() || dimmable.count(i) > 0;
        // A fade owns its controls while it runs (More row, a conditional section).
        const bool faded = (folded && fadeOwnsMore()) || fadeOwnsSection(item.section);
        for (auto* component : {item.widget, item.label}) {
            if (component == nullptr)
                continue;
            if (governed && !faded)
                component->setVisible(visible && !isHeldBySwap(*component)); // an arriving control waits its turn
            if (dims)
                markDimmed(*component, item.dimmed);
        }
        if (dims && item.widget != nullptr && dimHintsReady_)
            describeDimmed(*item.widget, item.dimmed);
    }
    for (int s = 0; s < (int)plan_.sections.size(); ++s)
        if (const auto& section = plan_.sections[(size_t)s]; section.header != nullptr && !fadeOwnsSection(s))
            section.header->setVisible(section.visible);
    if (moreButton_ != nullptr)
        moreButton_->setUnfolded(moreUnfolded_);
}

void CardBody::applyDimHints() {
    dimHintsReady_ = true;
    applyVisibility();
}

bool CardBody::isSwappedOut(const juce::Component& widget) const {
    for (int i = 0; i < (int)plan_.items.size(); ++i) {
        const auto& item = plan_.items[(size_t)i];
        if (item.widget == &widget)
            // An arriving control the swap motion still holds hidden keeps its cell (and its CV jack on it) too:
            // its jack must not drop to the gutter, over the graph, for the length of the shrink.
            return (item.swapGroup >= 0 && !item.shown && plan_.isOnCard(i)) ||
                   (plan_.isInAltGroup(i) && !plan_.isOnCard(i)) || (plan_.isOnCard(i) && isHeldBySwap(widget));
    }
    return false;
}

// Only the parameters a condition reads are listened to. Their callbacks can arrive on the audio
// thread, so they only queue a re-read on the message thread.
void CardBody::startWatchingConditions() {
    watched_ = plan_.watchedParameters(module_);
    for (auto* param : watched_)
        param->addListener(this);
}

// During an undo the processor may already be freed: then its parameters are not touched at all.
void CardBody::stopWatchingConditions(bool processorAlive) {
    cancelPendingUpdate();
    if (processorAlive)
        for (auto* param : watched_)
            param->removeListener(this);
    watched_.clear();
}

void CardBody::parameterValueChanged(int, float) { triggerAsyncUpdate(); }

void CardBody::parameterGestureChanged(int, bool) {}

void CardBody::handleAsyncUpdate() { refreshConditions(); }

// A swap or a dim keeps the card's height; a section appearing or going changes it, and then the card
// goes through the same resize path as any other growth (make room, and give it back on a shrink).
// A swap in place moves nothing else: the leaving controls are pictured before the card changes and shrink away,
// then the arriving ones grow (CardBodySwapMotion.cpp); a card that changed height lands at once.
void CardBody::refreshConditions() {
    if (watched_.empty())
        return;
    finishSwapMotion();
    const bool animate = canAnimateSwap();
    auto snapshot = animate ? snapshotForSwap() : SwapSnapshot();
    std::vector<bool> sectionWasVisible;
    for (const auto& section : plan_.sections)
        sectionWasVisible.push_back(section.visible);
    if (!plan_.evaluateConditions(module_).any)
        return;
    // A plain section that comes or goes fades (it is no swap): one going stays laid out until it has faded out, then
    // the card closes up; one coming is laid out at once and fades in. Everything else swaps or lands as before.
    beginSectionFades(sectionWasVisible);
    // When nothing but fading-out sections flipped, the card stays exactly as it is until they have gone.
    bool onlyLeaving = !startingSections_.empty();
    for (int s = 0; s < (int)plan_.sections.size(); ++s)
        if (plan_.sections[(size_t)s].visible != sectionWasVisible[(size_t)s] &&
            (!startingSections_.count(s) || plan_.sections[(size_t)s].visible))
            onlyLeaving = false;
    if (animate)
        pictureLeavingControls(snapshot);
    applyVisibility();
    // The fades start before the layout: a section coming is already shown (at alpha 0) when the card measures, so the
    // jacks of its knobs leave the gutter as they will stay.
    for (int s : std::exchange(startingSections_, {}))
        sectionFades_[(size_t)s].get()->setShown(plan_.sections[(size_t)s].visible);
    const int height = card_.getHeight();
    if (!onlyLeaving)
        card_.updateLayout();
    const bool resized = card_.getHeight() != height;
    if (resized)
        card_.owner.handleModuleResized(&card_);
    card_.repaint();
    if (animate && !resized)
        startSwapMotion(snapshot);
    if (onConditionsApplied)
        onConditionsApplied();
}

} // namespace synth
