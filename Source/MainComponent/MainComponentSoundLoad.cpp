// MainComponentSoundLoad.cpp -- Return or a double-click on a Sound in the Sounds view: the keyboard equivalent of
// dragging it onto a target (docs/layout/sound-browser.md#dragging-a-sound).
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "MainComponentAudioImport.h"
#include "Modules/SamplerModule.h"
#include "SoundBrowser/LoopStretchFile.h"
#include "UI/Graph/GraphDragDropController/GraphDragDropController.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Library/SoundBrowser/ShapeStamp.h"
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
// only the Sampler and the canvas take it. A loop lands on the track fitted to the song's tempo (and key, with Match
// key on) exactly as a drop on the track does; the Sampler and the canvas take the file as it is.
void MainComponentAudioImport::loadSoundFromBrowser(MainComponent& mc, const juce::File& sound,
                                                    const synth::soundbrowser::LoopTarget& loop) {
    juce::String message;
    if (auto* card = selectedSamplerCard(mc.graphEditor)) {
        card->filesDropped({sound.getFullPathName()}, 0, 0);
        message = "Loaded " + sound.getFileName() + " into the selected Sampler";
    } else if (const auto* track = mc.timelineDoc.getTrack(mc.timelinePanel.getSelectedTrackId());
               track != nullptr && track->kind == synth::TrackKind::Audio && synth::ui::isAudioSound(sound)) {
        const double playhead = std::max(0.0, mc.audioEngine.getTransport().getPositionSnapshot().ppq);
        const auto clipsBefore = track->clips.size();
        mc.importAudioFileToClip(track->id, playhead, synth::soundbrowser::stretchedLoopFile(sound, loop));
        const auto* after = mc.timelineDoc.getTrack(track->id);
        if (after != nullptr && after->clips.size() > clipsBefore)
            message = "Added " + sound.getFileName() + " to " + after->name + " at the playhead";
    } else {
        const auto size = GraphEditor::estimateModuleSize("Sampler");
        mc.graphEditor.getDragDropController().addSamplerLoadedWith(
            sound, mc.graphEditor.getViewportCentreInCanvasSpace() - juce::Point<int>(size.x / 2, size.y / 2));
        message = "Added a Sampler with " + sound.getFileName();
    }
    if (mc.toast_ && message.isNotEmpty())
        mc.toast_->show(message);
}

// Return on a shape: the selected Filter on Custom or LFO with a Custom wave of the same kind takes it, as a drop on
// the card would. A toast says where it went; with no fitting card nothing happens (the row's tooltip says why).
bool MainComponentAudioImport::stampShapeFromBrowser(MainComponent& mc, const synth::shapes::LibraryShape& shape) {
    const auto card = synth::ui::stampShapeOnSelectedCard(mc.graphEditor, shape);
    if (card.isEmpty())
        return false;
    if (mc.toast_)
        mc.toast_->show("Stamped " + shape.name + " on " + card);
    return true;
}
