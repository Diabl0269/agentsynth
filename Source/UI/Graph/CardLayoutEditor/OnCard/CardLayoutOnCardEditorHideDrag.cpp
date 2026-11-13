// CardLayoutOnCardEditorHideDrag.cpp -- the motion of "Drop to hide": the dragged control hovers on the zone while
// the pointer is over it, shrinks into the zone's centre when let go, and an undo or redo moves it out of and back into
// the zone. The card clips its own children, so a picture of the control (the hide proxy) is what sits on the zone.
// Under Reduce Motion, with Animations Off or with nothing on screen none of this runs: the control stays in the
// card and the usual hide shrink or fade plays where it was. docs/layout/module-card-layout.md#editing-a-layout.
#include "CardLayoutOnCardEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/ReducedMotion.h"
#include <utility>

namespace synth::ui {

namespace {
constexpr double kSnapMs = 140.0;
} // namespace

// Where the zone's centre is, in the card's own pixels (the overlay and the card share an origin, but the card's
// position is the one the cells are measured from).
juce::Point<int> CardLayoutOnCardEditor::hideZoneCentreInCard() const {
    const auto centre = hideZone_.getBounds().getCentre();
    return card_ != nullptr ? card_->getLocalPoint(this, centre) : centre;
}

// `cell`'s size, centred on the zone.
juce::Rectangle<int> CardLayoutOnCardEditor::hideZoneRectFor(juce::Rectangle<int> cell) const {
    return cell.withCentre(hideZoneCentreInCard());
}

// The pointer has gone below the card edge: the control's picture takes its place on the zone (a short glide from
// where the control was, as one thing carried over) and the real control hides behind it. The cell and its outline
// move with the picture, so the preview is where a release would hide it.
void CardLayoutOnCardEditor::enterHideZone() {
    const int cell = drag_.cell;
    if (cell < 0 || cell >= (int)cells_.size() || card_ == nullptr || hideProxy_ != nullptr || prefersReducedMotion() ||
        animationsOff() || !canAnimate())
        return;
    const auto& c = cells_[(size_t)cell];
    auto image = card_->createComponentSnapshot(c.rect, true, 2.0f);
    if (image.isNull())
        return;
    drag_.zoneFrom = c.rect;
    hideProxyKey_ = c.key;
    for (auto* part : {c.widget.getComponent(), c.label.getComponent()})
        if (part != nullptr) {
            part->setAlpha(0.0f);
            hideProxyParts_.emplace_back(part);
        }
    hideProxy_ =
        std::make_unique<control_motion::ShrinkGhost>(std::move(image), c.rect, control_motion::axisFor(c.rect), false);
    hideProxy_->setBounds(c.rect.withPosition(getLocalPoint(card_, c.rect.getPosition())));
    addAndMakeVisible(*hideProxy_);
    if (auto* outline = getOutlineForTest(c.key))
        hideProxy_->toBehind(outline);
    startGlide({{c.key, c.rect, hideZoneRectFor(c.rect), kSnapMs, false}});
}

// The pointer is back above the edge: the picture goes, the real control shows again and the usual preview takes
// over from where it stood in the card.
void CardLayoutOnCardEditor::leaveHideZone() {
    if (hideProxy_ == nullptr)
        return;
    glidePump_.stop();
    finishGlide_ = nullptr;
    glideFrame_ = nullptr;
    dropHideProxy();
    if (drag_.cell >= 0 && drag_.cell < (int)cells_.size() && !drag_.zoneFrom.isEmpty())
        moveCellTo(drag_.cell, drag_.zoneFrom);
}

// The picture goes and the real control shows again. Never moves the cell.
void CardLayoutOnCardEditor::dropHideProxy() {
    proxyReturning_ = false;
    hideProxyKey_ = {};
    hideProxy_.reset();
    for (auto& part : std::exchange(hideProxyParts_, {}))
        if (part != nullptr)
            part->setAlpha(1.0f);
}

// Released on the zone: the picture, already there, shrinks into the zone's centre while the control's outline stays on
// it; once it has gone the layout loses the control (as for Hide from card) and the cell goes back where it stood in
// the card first, so nothing is written from the zone. The zone is where an undo brings it back from.
void CardLayoutOnCardEditor::hideFromZone(int cell, juce::Rectangle<int> zoneFrom) {
    closePanel();
    flushNudge();
    if (finishGlide_)
        std::exchange(finishGlide_, nullptr)(); // lands on the zone, the picture with it
    const auto key = cells_[(size_t)cell].key;
    pendingHide_ = key;
    pendingHideRect_ = zoneFrom;
    pendingHideParts_ = std::exchange(hideProxyParts_, {});
    hiddenFrom_[key] = hideZoneCentreInCard();
    hideProxyKey_ = {};
    auto* ghost = ghosts_.add(hideProxy_.release());
    ghost->restart();
    ghostPump_.run(
        ghost->durationMs(), [this] { tickGhosts(); },
        [this] {
            ghosts_.clear();
            flushPendingHide();
        });
}

} // namespace synth::ui
