#include "SoundBrowser/SoundIndexScan.h"
#include "SoundBrowser/SoundNameGuess.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace synth::soundbrowser {

namespace {

bool isAudioExtension(const juce::String& ext) {
    return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac" || ext == "ogg" || ext == "mp3";
}

juce::String relativeFolderOf(const juce::File& file, const juce::File& root) {
    auto rel = file.getParentDirectory().getRelativePathFrom(root).replaceCharacter('\\', '/');
    return rel == "." ? juce::String() : rel;
}

// Header-only analysis: opens the reader (which parses the format header and metadata chunks) and never reads a
// sample.
void readAudioHeader(juce::AudioFormatManager& formats, const juce::File& file, SoundEntry& entry, bool& readable) {
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    readable = reader != nullptr;
    if (!readable)
        return;
    entry.sampleRate = reader->sampleRate;
    entry.lengthSeconds = reader->sampleRate > 0.0 ? (double)reader->lengthInSamples / reader->sampleRate : 0.0;

    const auto& meta = reader->metadataValues;
    if (!meta.containsKey(juce::WavAudioFormat::acidOneShot))
        return;
    const bool oneShot = meta[juce::WavAudioFormat::acidOneShot].getIntValue() != 0;
    double tempo = meta.getValue(juce::WavAudioFormat::acidTempo, "0").getDoubleValue();
    const int beats = meta.getValue(juce::WavAudioFormat::acidBeats, "0").getIntValue();
    if (tempo <= 0.0 && beats > 0 && entry.lengthSeconds > 0.0)
        tempo = beats * 60.0 / entry.lengthSeconds;
    if (meta[juce::WavAudioFormat::acidRootSet].getIntValue() != 0)
        entry.key = noteNameOf(meta[juce::WavAudioFormat::acidRootNote].getIntValue());
    if (oneShot) {
        entry.kind = SoundKind::OneShot;
        entry.tempo = 0.0;
    } else if (tempo >= 60.0 && tempo <= 200.0) {
        entry.kind = SoundKind::Loop;
        entry.tempo = tempo;
    }
}

SoundEntry makeEntry(const SoundPlace& place, const juce::File& file, juce::int64 size, juce::int64 mtime) {
    SoundEntry e;
    e.path = file.getFullPathName();
    e.placeId = place.id;
    e.placeName = place.name;
    e.folder = relativeFolderOf(file, place.root);
    e.name = file.getFileNameWithoutExtension();
    e.size = size;
    e.mtime = mtime;
    return e;
}

// Fills tags, tempo, key and kind from the names, then lets the file's own metadata overrule them.
void classify(SoundEntry& e, bool isInstrument, juce::AudioFormatManager* formats, const juce::File& file,
              bool& readable) {
    const auto guess = guessFromName(e.folder, e.name);
    e.tags = guess.tags;
    e.tempo = guess.tempo;
    e.key = guess.key;
    e.kind = isInstrument ? SoundKind::Instrument
                          : ((guess.tempo > 0.0 || guess.loopHint) ? SoundKind::Loop : SoundKind::OneShot);
    readable = true;
    if (isInstrument)
        return;
    const auto nameKey = e.key;
    readAudioHeader(*formats, file, e, readable);
    if (nameKey.isNotEmpty())
        e.key = nameKey;
}

struct Walk {
    const std::map<juce::String, SoundEntry>& previous;
    const ScanLimits& limits;
    juce::AudioFormatManager formats;
    ScanResult result;
    size_t nextProgress = 500;
};

bool visit(Walk& w, const SoundPlace& place, const juce::DirectoryEntry& item) {
    const auto file = item.getFile();
    const auto ext = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    const bool isSfz = ext == "sfz";
    if (!isSfz && !isAudioExtension(ext))
        return true;
    if (w.result.entries.size() >= w.limits.cap) {
        w.result.truncated = true;
        return false;
    }
    const juce::int64 size = item.getFileSize();
    const juce::int64 mtime = item.getModificationTime().toMilliseconds();
    auto entry = makeEntry(place, file, size, mtime);

    const auto old = w.previous.find(entry.path);
    if (old != w.previous.end() && old->second.size == size && old->second.mtime == mtime) {
        auto kept = old->second; // analysis is unchanged; the place may not be
        kept.placeId = entry.placeId;
        kept.placeName = entry.placeName;
        kept.folder = entry.folder;
        w.result.entries.push_back(std::move(kept));
    } else {
        bool readable = true;
        if (!isSfz && w.limits.headersRead != nullptr)
            w.limits.headersRead->fetch_add(1);
        classify(entry, isSfz, &w.formats, file, readable);
        if (!readable)
            return true; // not a readable audio file: nothing to preview or load
        w.result.entries.push_back(std::move(entry));
    }
    if (w.limits.progress && w.result.entries.size() >= w.nextProgress) {
        w.limits.progress(w.result.entries);
        w.nextProgress *= 2;
    }
    return !(w.limits.shouldExit && w.limits.shouldExit());
}

juce::var entryToVar(const SoundEntry& e) {
    auto* o = new juce::DynamicObject();
    o->setProperty("p", e.path);
    o->setProperty("pl", e.placeId);
    o->setProperty("pn", e.placeName);
    o->setProperty("f", e.folder);
    o->setProperty("n", e.name);
    o->setProperty("s", e.size);
    o->setProperty("m", e.mtime);
    o->setProperty("l", e.lengthSeconds);
    o->setProperty("r", e.sampleRate);
    o->setProperty("k", (int)e.kind);
    o->setProperty("t", e.tags.joinIntoString(","));
    o->setProperty("b", e.tempo);
    o->setProperty("y", e.key);
    return juce::var(o);
}

bool entryFromVar(const juce::var& v, SoundEntry& e) {
    if (!v.isObject())
        return false;
    e.path = v.getProperty("p", {}).toString();
    e.placeId = v.getProperty("pl", {}).toString();
    e.placeName = v.getProperty("pn", {}).toString();
    e.folder = v.getProperty("f", {}).toString();
    e.name = v.getProperty("n", {}).toString();
    e.size = (juce::int64)v.getProperty("s", 0);
    e.mtime = (juce::int64)v.getProperty("m", 0);
    e.lengthSeconds = (double)v.getProperty("l", 0.0);
    e.sampleRate = (double)v.getProperty("r", 0.0);
    const int kind = (int)v.getProperty("k", 0);
    e.kind = kind >= 0 && kind <= 2 ? (SoundKind)kind : SoundKind::OneShot;
    e.tags.addTokens(v.getProperty("t", {}).toString(), ",", "");
    e.tags.removeEmptyStrings();
    e.tempo = (double)v.getProperty("b", 0.0);
    e.key = v.getProperty("y", {}).toString();
    return e.path.isNotEmpty();
}

juce::String packText(const juce::File& dir, const char* key) {
    return juce::JSON::parse(dir.getChildFile("pack.json")).getProperty(key, {}).toString();
}

} // namespace

bool isSoundFile(const juce::File& file) {
    const auto ext = file.getFileExtension().trimCharactersAtStart(".").toLowerCase();
    return ext == "sfz" || isAudioExtension(ext);
}

std::vector<SoundPlace> discoverPlaces(const std::vector<juce::File>& packRoots,
                                       const std::vector<juce::File>& userFolders) {
    std::vector<SoundPlace> places;
    juce::StringArray seen;
    for (const auto& root : packRoots) {
        auto dirs = root.findChildFiles(juce::File::findDirectories, false);
        dirs.sort();
        for (const auto& dir : dirs) {
            if (!dir.getChildFile("pack.json").existsAsFile())
                continue;
            const auto id = packText(dir, "id");
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen.add(id);
            auto name = packText(dir, "name");
            places.push_back({id, name.isEmpty() ? id : name, dir, true, 0});
        }
    }
    for (const auto& folder : userFolders)
        places.push_back({"folder:" + folder.getFullPathName(), folder.getFileName(), folder, false, 0});
    return places;
}

ScanResult scanPlaces(const std::vector<SoundPlace>& places, const std::map<juce::String, SoundEntry>& previous,
                      const ScanLimits& limits) {
    Walk walk{previous, limits, {}, {}, 500};
    walk.formats.registerBasicFormats();
    for (const auto& place : places) {
        if (!place.root.isDirectory())
            continue;
        for (const auto& item : juce::RangedDirectoryIterator(place.root, true, "*", juce::File::findFiles)) {
            if (item.isHidden())
                continue;
            if (!visit(walk, place, item))
                return walk.result;
        }
    }
    return walk.result;
}

std::vector<SoundEntry> readIndexCache(const juce::File& file, bool& truncated) {
    std::vector<SoundEntry> out;
    truncated = false;
    if (!file.existsAsFile())
        return out;
    const auto root = juce::JSON::parse(file);
    const auto* list = root.getProperty("entries", {}).getArray();
    if (list == nullptr || (int)root.getProperty("version", 0) != 1)
        return out;
    truncated = (bool)root.getProperty("truncated", false);
    out.reserve((size_t)list->size());
    for (const auto& v : *list) {
        SoundEntry e;
        if (entryFromVar(v, e))
            out.push_back(std::move(e));
    }
    return out;
}

void writeIndexCache(const juce::File& file, const std::vector<SoundEntry>& entries, bool truncated) {
    juce::Array<juce::var> list;
    for (const auto& e : entries)
        list.add(entryToVar(e));
    auto* o = new juce::DynamicObject();
    o->setProperty("version", 1);
    o->setProperty("truncated", truncated);
    o->setProperty("entries", juce::var(list));
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile temp(file);
    if (temp.getFile().replaceWithText(juce::JSON::toString(juce::var(o), true)))
        temp.overwriteTargetFileWithTemporary();
}

} // namespace synth::soundbrowser
