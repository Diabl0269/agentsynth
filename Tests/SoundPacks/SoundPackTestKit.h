#pragma once

// Shared helpers for the sound pack tests: small zips built in memory, a fake fetcher that serves them, and an
// installer wired to a scratch folder. Nothing here touches the network or the real SoundPacks folder.
#include "Modules/Sampler/SoundPackLibrary.h"
#include "SoundPacks/SoundPackChecksum.h"
#include "SoundPacks/SoundPackInstaller.h"
#include <map>
#include <vector>

namespace sound_pack_test {

using synth::soundpacks::Fetcher;
using synth::soundpacks::FetchResult;
using synth::soundpacks::SoundPackInstaller;

class TempDir {
public:
    TempDir() {
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("agentsynth-packs-" + juce::Uuid().toString());
        dir_.createDirectory();
    }
    ~TempDir() { dir_.deleteRecursively(); }
    const juce::File& path() const { return dir_; }

private:
    juce::File dir_;
};

using Entries = std::vector<std::pair<juce::String, juce::String>>;

/** A zip holding the given (stored name, text) entries, built the way the hosted packs are. */
inline juce::MemoryBlock makeZip(const Entries& entries) {
    juce::ZipFile::Builder builder;
    for (const auto& [name, text] : entries)
        builder.addEntry(new juce::MemoryInputStream(text.toRawUTF8(), text.getNumBytesAsUTF8(), true), 5, name,
                         juce::Time::getCurrentTime());
    juce::MemoryBlock block;
    juce::MemoryOutputStream out(block, false);
    builder.writeToStream(out, nullptr);
    return block;
}

inline juce::String manifestJson(const juce::String& id, const juce::String& version) {
    return R"({"id":")" + id + R"(","name":"Pack )" + id + R"(","version":")" + version +
           R"(","licence":"CC0","credit":"Someone",
        "instruments":[{"id":"piano","name":"Grand","sfz":"piano/piano.sfz","category":"Keys"}]})";
}

/** A well-formed pack zip: pack.json, one instrument, a LICENSES/SOURCES.md and a marker file named for the version. */
inline juce::MemoryBlock goodPackZip(const juce::String& id, const juce::String& version) {
    return makeZip({{id + "/pack.json", manifestJson(id, version)},
                    {id + "/piano/piano.sfz", "<region> sample=x.wav"},
                    {id + "/LICENSES/SOURCES.md", "# Sources"},
                    {id + "/marker-" + version + ".txt", "marker"}});
}

inline juce::String indexJson(const juce::String& id, const juce::String& version, const juce::MemoryBlock& zip,
                              const juce::String& sha = {}) {
    const juce::String hash = sha.isNotEmpty() ? sha : synth::soundpacks::sha256Hex(zip.getData(), zip.getSize());
    return R"({"format":1,"packs":[{"id":")" + id + R"(","name":"Pack )" + id + R"(","version":")" + version +
           R"(","url":"https://example.test/)" + id + "-" + version + R"(.zip","sha256":")" + hash +
           R"(","downloadBytes":)" + juce::String((juce::int64)zip.getSize()) + R"(,"installedBytes":1000,
        "licence":"CC0 1.0","licenceUrl":"https://example.test/licence","credit":"Made by someone.",
        "instruments":[{"id":"piano","name":"Grand","category":"Keys"}]}]})";
}

/** Serves bodies by URL, in three chunks so progress is reported; an unknown URL answers 404 like the real fetcher. */
struct FakeServer {
    std::map<juce::String, juce::MemoryBlock> bodies;
    bool offline = false;
    std::function<void()> onChunk; ///< Runs after each chunk, to cancel mid-download.

    Fetcher fetcher() {
        return [this](const juce::String& url, const juce::File& dest, const synth::soundpacks::FetchProgress& progress,
                      const std::atomic<bool>& cancel) {
            FetchResult result;
            if (offline) {
                result.error = "Couldn't reach the server. Check your internet connection.";
                return result;
            }
            const auto found = bodies.find(url);
            if (found == bodies.end()) {
                result.error = "The download was not found on the server (error 404).";
                return result;
            }
            const auto& body = found->second;
            juce::FileOutputStream out(dest);
            const size_t chunk = juce::jmax<size_t>(1, body.getSize() / 3);
            for (size_t at = 0; at < body.getSize(); at += chunk) {
                if (cancel.load()) {
                    out.flush();
                    dest.deleteFile();
                    result.cancelled = true;
                    return result;
                }
                const size_t n = juce::jmin(chunk, body.getSize() - at);
                out.write(static_cast<const char*>(body.getData()) + at, n);
                progress((juce::int64)(at + n), (juce::int64)body.getSize());
                if (onChunk)
                    onChunk();
            }
            out.flush();
            result.ok = true;
            return result;
        };
    }
};

inline const char* const kIndexUrl = "https://example.test/index.json";

/** An installer on a scratch folder, synchronous, fed by a FakeServer. */
struct Rig {
    TempDir dir;
    FakeServer server;
    SoundPackInstaller installer;

    Rig() {
        synth::sampler::SoundPackLibrary::setRootsForTests(std::vector<juce::File>{dir.path()});
        installer.setInstallRootForTesting(dir.path());
        installer.setIndexUrlForTesting(kIndexUrl);
        installer.setFetcherForTesting(server.fetcher());
        installer.setRunInlineForTesting(true);
    }
    ~Rig() { synth::sampler::SoundPackLibrary::setRootsForTests(std::nullopt); }

    /** Publishes one pack on the fake server and loads the list. */
    juce::MemoryBlock publish(const juce::String& id, const juce::String& version, juce::MemoryBlock zip = {},
                              const juce::String& sha = {}) {
        if (zip.getSize() == 0)
            zip = goodPackZip(id, version);
        const auto json = indexJson(id, version, zip, sha);
        server.bodies[kIndexUrl] = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
        server.bodies["https://example.test/" + id + "-" + version + ".zip"] = zip;
        installer.refreshIndex();
        return zip;
    }

    /** Names of the entries directly under the install folder. */
    juce::StringArray rootEntries() const {
        juce::StringArray names;
        for (const auto& f : dir.path().findChildFiles(juce::File::findFilesAndDirectories, false))
            names.add(f.getFileName());
        names.sort(false);
        return names;
    }
};

} // namespace sound_pack_test
