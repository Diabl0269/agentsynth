#include "Modules/Sampler/SamplerInstrumentSlot.h"
#include "Modules/ModuleFileStateKeys.h"
#include "Modules/Sampler/SamplerNoteNames.h"
#include "Modules/Sampler/SfzReader.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "Project/DeferredAssetLoads.h"

namespace synth::sampler {

namespace {

namespace fk = synth::module_file_keys;

constexpr const char* kDefaultFilesName = "My instrument";
constexpr int kMinNameLength = 3;

// A dropped file's placement as saved; the later map editor writes the same shape.
juce::var regionToVar(const DroppedRegion& r) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("file", r.file);
    o->setProperty("lokey", r.loKey);
    o->setProperty("hikey", r.hiKey);
    o->setProperty("key", r.key);
    o->setProperty("lovel", r.loVel);
    o->setProperty("hivel", r.hiVel);
    return juce::var(o.get());
}

DroppedRegion regionFromVar(const juce::var& v) {
    DroppedRegion r;
    r.file = (int)v.getProperty("file", -1);
    r.loKey = juce::jlimit(0, 127, (int)v.getProperty("lokey", 0));
    r.hiKey = juce::jlimit(0, 127, (int)v.getProperty("hikey", 127));
    r.key = juce::jlimit(0, 127, (int)v.getProperty("key", 60));
    r.loVel = juce::jlimit(1, 127, (int)v.getProperty("lovel", 1));
    r.hiVel = juce::jlimit(1, 127, (int)v.getProperty("hivel", 127));
    return r;
}

std::vector<DroppedRegion> placeFiles(const juce::StringArray& files) {
    juce::StringArray present;
    std::vector<int> indexOf;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].isNotEmpty()) {
            present.add(files[i]);
            indexOf.push_back(i);
        }
    std::vector<DroppedRegion> regions;
    const auto specs = placeDroppedFiles(present);
    for (size_t k = 0; k < specs.size(); ++k)
        regions.push_back(
            {indexOf[k], specs[k].loKey, specs[k].hiKey, specs[k].rootKey, specs[k].loVel, specs[k].hiVel});
    return regions;
}

juce::String trimSeparators(juce::String text) {
    while (text.isNotEmpty() && !juce::CharacterFunctions::isLetterOrDigit(text.getLastCharacter()))
        text = text.dropLastCharacters(1);
    return text;
}

std::vector<RegionSpec> specsFor(const InstrumentSource& source) {
    std::vector<RegionSpec> specs;
    for (const auto& r : source.regions) {
        if (r.file < 0 || r.file >= source.files.size() || source.files[r.file].isEmpty())
            continue;
        RegionSpec spec;
        spec.samplePath = source.files[r.file];
        spec.loKey = r.loKey;
        spec.hiKey = r.hiKey;
        spec.rootKey = r.key;
        spec.loVel = r.loVel;
        spec.hiVel = r.hiVel;
        specs.push_back(spec);
    }
    return specs;
}

InstrumentLoad decodeSfz(const juce::File& sfz) {
    InstrumentLoad out;
    out.instrument = loadSfzInstrument(sfz, out.messages);
    return out;
}

InstrumentLoad decodePack(const InstrumentSource& source) {
    InstrumentLoad out;
    const auto found = SoundPackLibrary::find(source.packId, source.instrumentId);
    if (!found) {
        out.messages.add("The sound pack instrument " + source.packId + "/" + source.instrumentId + " was not found.");
        return out;
    }
    out = decodeSfz(found->sfz);
    if (out.instrument != nullptr)
        out.instrument->name = found->name;
    return out;
}

InstrumentLoad decodeFiles(const InstrumentSource& source) {
    InstrumentLoad out;
    const auto specs = specsFor(source);
    if (specs.empty()) {
        out.messages.add("None of the instrument's files could be found.");
        return out;
    }
    out.instrument = buildInstrument(InstrumentSource::nameForFiles(source.files), specs, out.messages);
    return out;
}

} // namespace

InstrumentSource InstrumentSource::fromSfz(const juce::File& sfz) {
    InstrumentSource s;
    s.kind = Kind::Sfz;
    s.sfzFolder = sfz.getParentDirectory();
    s.sfzRelative = sfz.getFileName();
    return s;
}

InstrumentSource InstrumentSource::fromPack(const juce::String& packId, const juce::String& instrumentId) {
    InstrumentSource s;
    s.kind = Kind::Pack;
    s.packId = packId;
    s.instrumentId = instrumentId;
    return s;
}

InstrumentSource InstrumentSource::fromFiles(const juce::StringArray& absolutePaths) {
    InstrumentSource s;
    s.kind = Kind::Files;
    s.files = absolutePaths;
    s.files.sortNatural();
    s.regions = placeFiles(s.files);
    return s;
}

// Case-insensitive common start of the names, minus trailing separators.
juce::String InstrumentSource::nameForFiles(const juce::StringArray& files) {
    juce::StringArray names;
    for (const auto& f : files)
        if (f.isNotEmpty())
            names.add(juce::File(f).getFileNameWithoutExtension());
    if (names.isEmpty())
        return kDefaultFilesName;
    if (names.size() == 1)
        return names[0];
    juce::String prefix = names[0];
    for (const auto& n : names) {
        int common = 0;
        while (common < prefix.length() && common < n.length() &&
               juce::CharacterFunctions::toLowerCase(prefix[common]) ==
                   juce::CharacterFunctions::toLowerCase(n[common]))
            ++common;
        prefix = prefix.substring(0, common);
    }
    prefix = trimSeparators(prefix);
    // "Piano_C" (the start of Piano_C3 and Piano_C4) loses the note letter left over after the separator.
    const int lastSeparator = prefix.lastIndexOfAnyOf(" _-.");
    if (lastSeparator > 0 && prefix.length() - lastSeparator - 1 < kMinNameLength)
        prefix = trimSeparators(prefix.substring(0, lastSeparator));
    return prefix.length() >= kMinNameLength ? prefix : juce::String(kDefaultFilesName);
}

void writeInstrumentState(const InstrumentSource& s, juce::DynamicObject& state) {
    switch (s.kind) {
    case InstrumentSource::Kind::None:
        break;
    case InstrumentSource::Kind::Sfz:
        state.setProperty(fk::kInstrumentFolder, s.sfzFolder.getFullPathName());
        state.setProperty(fk::kInstrumentSfz, s.sfzRelative);
        break;
    case InstrumentSource::Kind::Pack:
        state.setProperty(fk::kInstrumentPack, s.packId + "/" + s.instrumentId);
        break;
    case InstrumentSource::Kind::Files: {
        juce::Array<juce::var> files, regions;
        for (const auto& f : s.files)
            files.add(f);
        for (const auto& r : s.regions)
            regions.add(regionToVar(r));
        state.setProperty(fk::kInstrumentFiles, files);
        state.setProperty(fk::kInstrumentRegions, regions);
        break;
    }
    }
}

InstrumentSource readInstrumentState(const juce::DynamicObject& state) {
    const juce::String folder = state.getProperty(fk::kInstrumentFolder).toString();
    const juce::String sfz = state.getProperty(fk::kInstrumentSfz).toString();
    const juce::String pack = state.getProperty(fk::kInstrumentPack).toString();
    if (folder.isNotEmpty() && sfz.isNotEmpty() && juce::File::isAbsolutePath(folder)) {
        InstrumentSource s;
        s.kind = InstrumentSource::Kind::Sfz;
        s.sfzFolder = juce::File(folder);
        s.sfzRelative = sfz;
        return s;
    }
    if (pack.contains("/"))
        return InstrumentSource::fromPack(pack.upToFirstOccurrenceOf("/", false, false),
                                          pack.fromFirstOccurrenceOf("/", false, false));
    if (const auto* list = state.getProperty(fk::kInstrumentFiles).getArray(); list != nullptr && !list->isEmpty()) {
        InstrumentSource s;
        s.kind = InstrumentSource::Kind::Files;
        for (const auto& f : *list)
            s.files.add(f.toString());
        if (const auto* regions = state.getProperty(fk::kInstrumentRegions).getArray()) {
            for (const auto& r : *regions)
                s.regions.push_back(regionFromVar(r));
        } else {
            s.regions = placeFiles(s.files);
        }
        return s;
    }
    return {};
}

InstrumentLoad decodeInstrument(const InstrumentSource& source) {
    switch (source.kind) {
    case InstrumentSource::Kind::Sfz:
        return decodeSfz(source.sfzFolder.getChildFile(source.sfzRelative));
    case InstrumentSource::Kind::Pack:
        return decodePack(source);
    case InstrumentSource::Kind::Files:
        return decodeFiles(source);
    case InstrumentSource::Kind::None:
        break;
    }
    return {};
}

// A failed restore still keeps `source` for saving (silent player, messages say why); a failed explicit load keeps
// what was loaded.
bool InstrumentSlot::install(const InstrumentSource& source, InstrumentLoad&& result, bool isRestore) {
    ++loadSerial_;
    messages_ = std::move(result.messages);
    if (result.instrument == nullptr) {
        if (isRestore) {
            loaded_ = source;
            player_.publish(nullptr);
        }
        return false;
    }
    loaded_ = source;
    player_.publish(result.instrument);
    return true;
}

bool InstrumentSlot::load(const InstrumentSource& source) {
    ++serial_; // a decode still pending for an earlier choice no longer applies
    pending_ = false;
    return install(source, decodeInstrument(source), false);
}

// Inside a project open on screen the decode runs on a worker and installs here later, unless something loaded or
// cleared the slot since (the serial moved) or the module is gone (the weak reference).
bool InstrumentSlot::deferRestore(const InstrumentSource& source) {
    auto* loads = DeferredAssetLoads::active();
    if (loads == nullptr)
        return false;
    const int serial = ++serial_;
    pendingSource_ = source;
    pending_ = true;
    juce::WeakReference<InstrumentSlot> self(this);
    loads->enqueue(DeferredAssetLoads::Kind::Sample, owner_, [self, serial, source] {
        auto decoded = std::make_shared<InstrumentLoad>(decodeInstrument(source));
        return DeferredAssetLoads::Install([self, serial, source, decoded] {
            auto* slot = self.get();
            if (slot == nullptr || slot->serial_ != serial)
                return;
            slot->pending_ = false;
            slot->install(source, std::move(*decoded), true);
        });
    });
    return true;
}

bool InstrumentSlot::restore(const InstrumentSource& source) {
    if (source.kind == InstrumentSource::Kind::None)
        return false;
    if (deferRestore(source))
        return true;
    ++serial_;
    pending_ = false;
    return install(source, decodeInstrument(source), true);
}

void InstrumentSlot::clear() {
    ++serial_;
    ++loadSerial_;
    pending_ = false;
    loaded_ = {};
    messages_.clear();
    player_.publish(nullptr);
}

} // namespace synth::sampler
