#pragma once

#include "SoundBrowser/SoundEntry.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

namespace synth::ui {

/** The filters the user has switched on in the Sounds view, apart from any widget: the panel edits it, the chip row
 *  lists it, and `toFilter` turns it into the index's `SoundFilter`. */
class SoundFilterModel {
public:
    static constexpr double kMinTempo = 60.0;
    static constexpr double kMaxTempo = 200.0;

    /** One removable chip: a stable id, the words on it and its tooltip. */
    struct Chip {
        juce::String id;
        juce::String text;
        juce::String tooltip;
    };

    // ---- What is on -------------------------------------------------------------------------------------
    bool hasKind(synth::soundbrowser::SoundKind kind) const { return kinds_.count(kind) > 0; }
    void setKind(synth::soundbrowser::SoundKind kind, bool on);
    bool hasTag(const juce::String& tag) const { return tags_.contains(tag, true); }
    void setTag(const juce::String& tag, bool on);
    std::optional<std::pair<int, int>> tempoRange() const { return tempo_; }
    /** Clamped to 60..200 and ordered; an empty optional switches the range off. */
    void setTempoRange(std::optional<std::pair<int, int>> range);
    bool nearSongTempo() const { return nearSongTempo_; }
    void setNearSongTempo(bool on) { nearSongTempo_ = on; }
    /** The key chosen from the menu ("A minor"); empty for any. Choosing one switches Song key off. */
    const juce::String& pickedKey() const { return pickedKey_; }
    void setPickedKey(const juce::String& key);
    bool useSongKey() const { return useSongKey_; }
    /** Needs a song key to be on; switches the picked key off. */
    void setUseSongKey(bool on);
    synth::soundbrowser::SoundLength length() const { return length_; }
    void setLength(synth::soundbrowser::SoundLength length) { length_ = length; }
    const juce::String& placeId() const { return placeId_; }
    const juce::String& placeName() const { return placeName_; }
    void setPlace(const juce::String& id, const juce::String& name);

    /** The song's key when the project has one ("" otherwise), and the song's tempo for Near song tempo. */
    void setSongKey(const juce::String& key);
    const juce::String& songKey() const { return songKey_; }
    void setSongTempo(double bpm) { songTempo_ = bpm; }

    // ---- Reading it -------------------------------------------------------------------------------------
    int count() const;
    std::vector<Chip> chips() const;
    /** Switches off the filter a chip stands for. */
    void remove(const juce::String& chipId);
    void clearAll();
    synth::soundbrowser::SoundFilter toFilter() const;

    static juce::String kindWord(synth::soundbrowser::SoundKind kind);
    static juce::String lengthWord(synth::soundbrowser::SoundLength length);

private:
    std::set<synth::soundbrowser::SoundKind> kinds_;
    juce::StringArray tags_;
    std::optional<std::pair<int, int>> tempo_;
    bool nearSongTempo_ = false;
    juce::String pickedKey_;
    bool useSongKey_ = false;
    juce::String songKey_;
    double songTempo_ = 120.0;
    synth::soundbrowser::SoundLength length_ = synth::soundbrowser::SoundLength::Any;
    juce::String placeId_, placeName_;
};

} // namespace synth::ui
