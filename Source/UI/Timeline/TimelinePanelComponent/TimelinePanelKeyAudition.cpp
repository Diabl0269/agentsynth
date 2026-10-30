// TimelinePanelComponent: the hooks of the two key pickers the panel hosts, the transport bar's Song key and the piano
// roll's Scale assist Key. Both play the scale of a hovered key (KeyAuditioner); this unit says where.
#include "Timeline/SongKeyNotes.h"
#include "TimelinePanelComponent.h"

namespace synth::ui {

namespace {

// A track a scale can be heard through: a MIDI track with an instrument bound, and not a Drum Kit (its pads are not a
// keyboard).
bool hasInstrumentToHear(const TrackHeaderHost* host, const synth::Track* track) {
    return host != nullptr && track != nullptr && track->kind == synth::TrackKind::Midi &&
           track->bindingUuid.isNotEmpty() && !host->isDrumKitTrack(track->id);
}

} // namespace

// The Song key field: Detect from notes reads every MIDI track but drum kits. A hovered key plays with the default
// sound unless the picker's Preview sound combo chose one of the instrument tracks listed here. The piano roll's Key
// picker plays through the open clip's own track (the path a note click takes), or the default sound when that track
// has no instrument to hear.
void TimelinePanelComponent::initSongKeyField() {
    auto& songKeyField = transportBar_.getSongKeyField();
    songKeyField.pitchClassWeights = [this] {
        if (doc_ == nullptr)
            return std::array<double, 12>{};
        return synth::songPitchClassWeights(*doc_, [this](const synth::Track& track) {
            return trackHeaderHost_ != nullptr && trackHeaderHost_->isDrumKitTrack(track.id);
        });
    };
    songKeyField.previewTracks = [this] {
        std::vector<SongKeyField::PreviewTrack> tracks;
        if (doc_ != nullptr)
            for (const auto& track : doc_->getTracks())
                if (hasInstrumentToHear(trackHeaderHost_, &track))
                    tracks.push_back({track.id, track.name});
        return tracks;
    };
    songKeyField.auditionNote = [this](synth::TrackId track, int pitch, int velocity, bool on) {
        if (trackHeaderHost_ != nullptr)
            trackHeaderHost_->auditionTrackNote(track, pitch, velocity, on);
    };

    pianoRoll_.getScaleAssistPanel().getAuditioner().resolveTarget = [this]() -> KeyAuditioner::Target {
        const auto clip = pianoRoll_.isOpen() ? pianoRoll_.getClipId() : synth::ClipId{};
        if (doc_ == nullptr || !clip.isValid() || !hasInstrumentToHear(trackHeaderHost_, doc_->getTrackForClip(clip)))
            return {};
        return {[this, clip](int pitch, int velocity, bool on) {
            auditionClipNote(clip, pitch, (float)velocity / 127.0f, on);
        }};
    };
}

} // namespace synth::ui
