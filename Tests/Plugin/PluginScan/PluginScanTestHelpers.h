#pragma once

// Shared test doubles and helpers for the PluginScanService test suite (Tests/Plugin/PluginScan/PluginScan*Tests.cpp).

#include "../../StubPluginInstance.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <thread>
#include <vector>

using synth::HostedPluginBackend;
using synth::HostedPluginModule;
using synth::PluginIdentity;
using synth::PluginScanService;

namespace {

constexpr const char* kAlpha = "/plugins/Alpha.vst3";
constexpr const char* kBeta = "/plugins/Beta.vst3";
constexpr const char* kCrasher = "/plugins/Crasher.vst3";

/** Pumps the message loop until `predicate` holds — the bounded-poll idiom from HostedPluginTests.
 *  Needed because scanAsync posts its progress and completion callbacks rather than calling them. */
template <typename Predicate>
bool pumpUntil(Predicate predicate, int timeoutMs = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

/** The document a healthy child process prints: KnownPluginList's own XML shape. */
juce::String descriptionXml(const juce::String& name, int uid, const juce::String& fileOrIdentifier,
                            const juce::String& format = "VST3") {
    juce::PluginDescription description;
    description.name = name;
    description.pluginFormatName = format;
    description.uniqueId = uid;
    description.deprecatedUid = uid;
    description.fileOrIdentifier = fileOrIdentifier;
    description.manufacturerName = "Test Labs";
    description.version = "1.0.0";

    juce::KnownPluginList list;
    list.addType(description);
    auto xml = list.createXml();
    return xml != nullptr ? xml->toString() : juce::String();
}

/** A ChildLauncher over a canned map, counting the launches so "was this candidate probed at all?"
 *  is directly observable — which is the whole assertion behind blacklisting. */
struct FakeLauncher {
    std::map<juce::String, juce::String> xmlByFile; // absent = "this one crashes"
    std::vector<juce::String> launched;
    int delayMs = 0;

    PluginScanService::ChildLauncher fn() {
        return [this](const juce::String&, const juce::String& fileOrIdentifier, int, juce::String& xmlOut) {
            launched.push_back(fileOrIdentifier);
            if (delayMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            const auto it = xmlByFile.find(fileOrIdentifier);
            if (it == xmlByFile.end())
                return false;
            xmlOut = it->second;
            return true;
        };
    }

    int launchCountFor(const juce::String& fileOrIdentifier) const {
        return (int)std::count(launched.begin(), launched.end(), fileOrIdentifier);
    }
};

PluginScanService::CandidateSource candidates(juce::StringArray files) {
    return [files](const juce::String&) { return files; };
}

/** Runs one scan to completion and hands back its Result. */
PluginScanService::Result scanToCompletion(PluginScanService& service,
                                           const juce::StringArray& formats = juce::StringArray("VST3")) {
    PluginScanService::Result result;
    bool done = false;
    service.scanAsync(formats, {}, [&](const PluginScanService::Result& r) {
        result = r;
        done = true;
    });
    EXPECT_TRUE(pumpUntil([&] { return done; })) << "the scan never completed";
    return result;
}

/** The real backend's resolveIdentity (so the scan service is genuinely in the loop) with the stub's
 *  instance creation (so no third-party binary is ever loaded). */
class ScanningStubBackend : public synth::DefaultHostedPluginBackend {
public:
    using synth::DefaultHostedPluginBackend::createInstanceAsync;

    void createInstanceAsync(const juce::PluginDescription& description, double, int,
                             InstanceCallback callback) override {
        if (callback == nullptr)
            return;
        lastDescription = description;
        auto sharedCallback = std::make_shared<InstanceCallback>(std::move(callback));
        juce::MessageManager::callAsync([sharedCallback] {
            (*sharedCallback)(std::make_unique<synth::test::StubPluginInstance>(2, 2), juce::String());
        });
    }

    juce::PluginDescription lastDescription;
};

int entryIndexForText(const ModuleLibraryComponent& library, const juce::String& text) {
    for (int i = 0; i < library.getEntryCount(); ++i)
        if (library.getEntryText(i) == text)
            return i;
    return -1;
}

} // namespace
