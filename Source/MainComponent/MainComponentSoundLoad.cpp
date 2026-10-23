// MainComponentSoundLoad.cpp -- Return or a double-click on a Sound in the Sounds view: the keyboard equivalent of
// dragging it onto a target (docs/layout/sound-browser.md#dragging-a-sound).
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "Modules/SamplerModule.h"
#include "UI/Graph/GraphDragDropController/GraphDragDropController.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/SoundBrowser/SoundDragPayload.h"

namespace {

// The Sampler card on the canvas when exactly one node is selected and it is a Sampler, else null.
ModuleComponent* selectedSamplerCard(GraphEditor& editor) {
    const auto selected = editor.getSelectedNodes();
    if (selected.size() != 1)
        return nullptr;
    for (auto* card : editor.getModuleComponents())
        if (card->getNodeId() == selected.front())
            return dynamic_cast<SamplerModule*>(card->getModule()) != nullptr ? card : nullptr;
    return nullptr;
}

} // namespace

// The first target that fits wins: the selected Sampler (replaces its sample in one undo step), the selected audio
// track (a clip at the playhead), else a new Sampler in the middle of the visible canvas. An .sfz is an instrument:
// only the Sampler and the canvas take it.
void MainComponent::loadSoundFromBrowser(const juce::File& sound) {
    juce::String message;
    if (auto* card = selectedSamplerCard(graphEditor)) {
        card->filesDropped({sound.getFullPathName()}, 0, 0);
        message = "Loaded " + sound.getFileName() + " into the selected Sampler";
    } else if (const auto* track = timelineDoc.getTrack(timelinePanel.getSelectedTrackId());
               track != nullptr && track->kind == synth::TrackKind::Audio && synth::ui::isAudioSound(sound)) {
        const double playhead = std::max(0.0, audioEngine.getTransport().getPositionSnapshot().ppq);
        const auto clipsBefore = track->clips.size();
        importAudioFileToClip(track->id, playhead, sound);
        const auto* after = timelineDoc.getTrack(track->id);
        if (after != nullptr && after->clips.size() > clipsBefore)
            message = "Added " + sound.getFileName() + " to " + after->name + " at the playhead";
    } else {
        const auto size = GraphEditor::estimateModuleSize("Sampler");
        graphEditor.getDragDropController().addSamplerLoadedWith(sound, graphEditor.getViewportCentreInCanvasSpace() -
                                                                            juce::Point<int>(size.x / 2, size.y / 2));
        message = "Added a Sampler with " + sound.getFileName();
    }
    if (toast_ && message.isNotEmpty())
        toast_->show(message);
}
