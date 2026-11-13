// SoundFilterModel.cpp -- which filters are on, the chips that stand for them, and the index filter they make.
#include "UI/Library/SoundBrowser/SoundFilterModel.h"
#include <algorithm>

namespace synth::ui {

using synth::soundbrowser::SoundKind;
using synth::soundbrowser::SoundLength;

juce::String SoundFilterModel::kindWord(SoundKind kind) {
    switch (kind) {
    case SoundKind::OneShot:
        return "One-shots";
    case SoundKind::Loop:
        return "Loops";
    case SoundKind::Instrument:
        return "Instruments";
    }
    return {};
}

juce::String SoundFilterModel::lengthWord(SoundLength length) {
    switch (length) {
    case SoundLength::Any:
        return "Any length";
    case SoundLength::Under1:
        return "Under 1 s";
    case SoundLength::OneToFour:
        return "1-4 s";
    case SoundLength::Over4:
        return "Over 4 s";
    }
    return {};
}

void SoundFilterModel::setKind(SoundKind kind, bool on) {
    if (on)
        kinds_.insert(kind);
    else
        kinds_.erase(kind);
}

void SoundFilterModel::setTag(const juce::String& tag, bool on) {
    if (on)
        tags_.addIfNotAlreadyThere(tag, true);
    else
        tags_.removeString(tag, true);
}

void SoundFilterModel::setTempoRange(std::optional<std::pair<int, int>> range) {
    if (!range.has_value()) {
        tempo_.reset();
        return;
    }
    int lo = juce::jlimit((int)kMinTempo, (int)kMaxTempo, range->first);
    int hi = juce::jlimit((int)kMinTempo, (int)kMaxTempo, range->second);
    if (lo > hi)
        std::swap(lo, hi);
    tempo_ = std::make_pair(lo, hi);
}

void SoundFilterModel::setPickedKey(const juce::String& key) {
    pickedKey_ = key;
    if (key.isNotEmpty())
        useSongKey_ = false;
}

void SoundFilterModel::setUseSongKey(bool on) {
    useSongKey_ = on && songKey_.isNotEmpty();
    if (useSongKey_)
        pickedKey_ = {};
}

void SoundFilterModel::setSongKey(const juce::String& key) {
    songKey_ = key;
    if (key.isEmpty())
        useSongKey_ = false;
}

void SoundFilterModel::setPlace(const juce::String& id, const juce::String& name) {
    placeId_ = id;
    placeName_ = id.isEmpty() ? juce::String() : name;
}

int SoundFilterModel::count() const { return (int)chips().size(); }

std::vector<SoundFilterModel::Chip> SoundFilterModel::chips() const {
    std::vector<Chip> out;
    const auto add = [&out](const juce::String& id, const juce::String& text) {
        out.push_back({id, text, "Remove " + text + " filter"});
    };
    for (const auto kind : {SoundKind::OneShot, SoundKind::Loop, SoundKind::Instrument})
        if (hasKind(kind))
            add("kind:" + juce::String((int)kind), kindWord(kind));
    for (const auto& tag : tags_)
        add("tag:" + tag, tag);
    if (tempo_.has_value())
        add("tempo", juce::String(tempo_->first) + "-" + juce::String(tempo_->second) + " BPM");
    if (nearSongTempo_)
        add("nearTempo", "Near song tempo");
    if (useSongKey_)
        add("songKey", "Song key");
    else if (pickedKey_.isNotEmpty())
        add("key", pickedKey_);
    if (length_ != SoundLength::Any)
        add("length", lengthWord(length_));
    if (placeId_.isNotEmpty())
        add("place", placeName_.isEmpty() ? placeId_ : placeName_);
    return out;
}

void SoundFilterModel::remove(const juce::String& id) {
    if (id.startsWith("kind:"))
        setKind((SoundKind)id.fromFirstOccurrenceOf(":", false, false).getIntValue(), false);
    else if (id.startsWith("tag:"))
        setTag(id.fromFirstOccurrenceOf(":", false, false), false);
    else if (id == "tempo")
        tempo_.reset();
    else if (id == "nearTempo")
        nearSongTempo_ = false;
    else if (id == "songKey")
        useSongKey_ = false;
    else if (id == "key")
        pickedKey_ = {};
    else if (id == "length")
        length_ = SoundLength::Any;
    else if (id == "place")
        setPlace({}, {});
}

void SoundFilterModel::clearAll() {
    kinds_.clear();
    tags_.clear();
    tempo_.reset();
    nearSongTempo_ = false;
    pickedKey_ = {};
    useSongKey_ = false;
    length_ = SoundLength::Any;
    setPlace({}, {});
}

synth::soundbrowser::SoundFilter SoundFilterModel::toFilter() const {
    synth::soundbrowser::SoundFilter f;
    f.kinds = kinds_;
    f.tags = tags_;
    f.placeId = placeId_;
    if (tempo_.has_value())
        f.tempo = std::make_pair((double)tempo_->first, (double)tempo_->second);
    if (nearSongTempo_)
        f.nearTempo = songTempo_;
    f.key = useSongKey_ ? songKey_ : pickedKey_;
    f.length = length_;
    return f;
}

} // namespace synth::ui
