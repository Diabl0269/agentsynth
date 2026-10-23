// SoundDropAdapter.cpp -- a dragged Sound handed to the component's own file-drop methods.
#include "UI/Library/SoundBrowser/SoundDropAdapter.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"

namespace synth::ui {

SoundDropAdapter::SoundDropAdapter() { SoundDragState::addListener(this); }

SoundDropAdapter::~SoundDropAdapter() { SoundDragState::removeListener(this); }

bool SoundDropAdapter::isInterestedInDragSource(const SourceDetails& details) {
    auto* target = fileTarget();
    return target != nullptr && isSoundPayload(details.description) &&
           target->isInterestedInFileDrag({soundFileFromPayload(details.description).getFullPathName()});
}

void SoundDropAdapter::itemDragEnter(const SourceDetails& details) {
    if (auto* target = fileTarget())
        target->fileDragEnter({soundFileFromPayload(details.description).getFullPathName()}, details.localPosition.x,
                              details.localPosition.y);
}

void SoundDropAdapter::itemDragMove(const SourceDetails& details) {
    if (auto* target = fileTarget())
        target->fileDragMove({soundFileFromPayload(details.description).getFullPathName()}, details.localPosition.x,
                             details.localPosition.y);
}

void SoundDropAdapter::itemDragExit(const SourceDetails& details) {
    if (auto* target = fileTarget())
        target->fileDragExit({soundFileFromPayload(details.description).getFullPathName()});
}

void SoundDropAdapter::itemDropped(const SourceDetails& details) {
    if (auto* target = fileTarget())
        target->filesDropped({soundFileFromPayload(details.description).getFullPathName()}, details.localPosition.x,
                             details.localPosition.y);
}

// The outline asks the file target the question a drop would, so a card that would refuse the file shows nothing.
void SoundDropAdapter::soundDragChanged() {
    auto* target = fileTarget();
    const bool show = SoundDragState::isActive() && target != nullptr &&
                      target->isInterestedInFileDrag({SoundDragState::file().getFullPathName()});
    if (show == outline_)
        return;
    outline_ = show;
    if (auto* component = dynamic_cast<juce::Component*>(this))
        component->repaint();
}

} // namespace synth::ui
