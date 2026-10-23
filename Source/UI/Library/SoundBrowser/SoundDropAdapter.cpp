// SoundDropAdapter.cpp -- a dragged Sound handed to the component's own file-drop methods.
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include "UI/Library/SoundBrowser/ShapeDragPayload.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"

namespace synth::ui {

SoundDropAdapter::SoundDropAdapter() { SoundDragState::addListener(this); }

SoundDropAdapter::~SoundDropAdapter() { SoundDragState::removeListener(this); }

bool SoundDropAdapter::isInterestedInDragSource(const SourceDetails& details) {
    if (isShapePayload(details.description)) {
        const auto ref = shapeFromPayload(details.description);
        auto* shapeTarget = shapeDropTarget();
        return ref.has_value() && shapeTarget != nullptr && shapeTarget->acceptsShapeDrop(ref->kind);
    }
    auto* target = fileTarget();
    return target != nullptr && isSoundPayload(details.description) &&
           target->isInterestedInFileDrag({soundFileFromPayload(details.description).getFullPathName()});
}

void SoundDropAdapter::itemDragEnter(const SourceDetails& details) {
    if (isShapePayload(details.description)) {
        const auto ref = shapeFromPayload(details.description);
        if (auto* shapeTarget = shapeDropTarget(); shapeTarget != nullptr && ref.has_value())
            shapeTarget->shapeDragOver(true, ref->name);
        return;
    }
    if (auto* target = fileTarget())
        target->fileDragEnter({soundFileFromPayload(details.description).getFullPathName()}, details.localPosition.x,
                              details.localPosition.y);
}

void SoundDropAdapter::itemDragMove(const SourceDetails& details) {
    if (isShapePayload(details.description))
        return;
    if (auto* target = fileTarget())
        target->fileDragMove({soundFileFromPayload(details.description).getFullPathName()}, details.localPosition.x,
                             details.localPosition.y);
}

void SoundDropAdapter::itemDragExit(const SourceDetails& details) {
    if (isShapePayload(details.description)) {
        if (auto* shapeTarget = shapeDropTarget())
            shapeTarget->shapeDragOver(false, {});
        return;
    }
    if (auto* target = fileTarget())
        target->fileDragExit({soundFileFromPayload(details.description).getFullPathName()});
}

// The curve is looked up by name now, so a shape deleted or replaced during the drag drops what is there.
void SoundDropAdapter::itemDropped(const SourceDetails& details) {
    if (isShapePayload(details.description)) {
        auto* shapeTarget = shapeDropTarget();
        const auto ref = shapeFromPayload(details.description);
        if (shapeTarget == nullptr || !ref.has_value())
            return;
        shapeTarget->shapeDragOver(false, {});
        auto& library = synth::shapes::ShapeLibrary::shared();
        if (const auto shape = library.find(ref->kind, ref->factory, ref->name);
            shape.has_value() && shapeTarget->acceptsShapeDrop(ref->kind)) {
            shapeTarget->shapeDropped(*shape);
            library.noteUsed(*shape);
        }
        return;
    }
    if (auto* target = fileTarget())
        target->filesDropped({soundFileFromPayload(details.description).getFullPathName()}, details.localPosition.x,
                             details.localPosition.y);
}

// The outline asks the file target the question a drop would, so a card that would refuse the file shows nothing.
void SoundDropAdapter::soundDragChanged() {
    auto* target = fileTarget();
    auto* shapeTarget = shapeDropTarget();
    const bool show = SoundDragState::isShapeActive()
                          ? shapeTarget != nullptr && shapeTarget->acceptsShapeDrop(SoundDragState::shapeKind())
                          : SoundDragState::isActive() && target != nullptr &&
                                target->isInterestedInFileDrag({SoundDragState::file().getFullPathName()});
    if (show == outline_)
        return;
    outline_ = show;
    if (auto* component = dynamic_cast<juce::Component*>(this))
        component->repaint();
}

} // namespace synth::ui
