// HostedPluginModuleOutputs.cpp -- the output side of HostedPluginModule: switching on every output bus the plugin
// accepts, the shown/joined model, the jack map and labels that follow it, and its persistence.
//
// A raw node output channel is the instance's own output channel index. Nothing is ever compacted, so a cable keeps
// its channel when another output is shown or hidden.

#include "HostedPluginModule.h"
#include <algorithm>

namespace synth {

namespace {

// Bus names a plugin gives every aux output by default say nothing about the sound: those fall back to "Out 3/4".
bool isGenericBusName(const juce::String& name) {
    const auto trimmed = name.toLowerCase().removeCharacters("0123456789 -_.#");
    static const char* const generic[] = {"",          "out",       "output",       "outputs", "aux",
                                          "bus",       "auxoutput", "auxout",       "auxbus",  "outputbus",
                                          "auxiliary", "stereoout", "stereooutput", "monoout"};
    return std::any_of(std::begin(generic), std::end(generic), [&](const char* g) { return trimmed == g; });
}

// The layouts a disabled output bus is switched on with, best first: its own default, stereo, mono.
std::vector<juce::AudioChannelSet> candidateSets(const juce::AudioProcessor::Bus& bus) {
    std::vector<juce::AudioChannelSet> sets;
    for (const auto& set : {bus.getDefaultLayout(), juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono()})
        if (!set.isDisabled() && std::find(sets.begin(), sets.end(), set) == sets.end())
            sets.push_back(set);
    return sets;
}

} // namespace

void HostedPluginModule::enableOutputBuses(juce::AudioPluginInstance& instance) {
    // Wider than the node on a side: main buses only on that side (a plugin that refuses the reduced layout is
    // left as it was, and refused by publishInstance).
    auto reduceToMain = [&instance](bool inputs, bool outputs) {
        auto candidate = instance.getBusesLayout();
        if (inputs)
            for (int i = 1; i < candidate.inputBuses.size(); ++i)
                candidate.inputBuses.getReference(i) = juce::AudioChannelSet::disabled();
        if (outputs)
            for (int i = 1; i < candidate.outputBuses.size(); ++i)
                candidate.outputBuses.getReference(i) = juce::AudioChannelSet::disabled();
        if (instance.checkBusesLayoutSupported(candidate))
            instance.setBusesLayout(candidate);
    };
    if (instance.getTotalNumInputChannels() > kMaxPluginInputChannels ||
        instance.getTotalNumOutputChannels() > kMaxPluginOutputChannels)
        reduceToMain(instance.getTotalNumInputChannels() > kMaxPluginInputChannels,
                     instance.getTotalNumOutputChannels() > kMaxPluginOutputChannels);

    // What the instance runs with by default: an old project (saved before outputs were chosen) shows exactly these.
    defaultEnabledOutputBuses_.clear();
    for (int i = 0; i < instance.getBusCount(false); ++i)
        if (instance.getBus(false, i)->isEnabled())
            defaultEnabledOutputBuses_.push_back(i);

    const auto layout = instance.getBusesLayout();
    int total = instance.getTotalNumOutputChannels();
    std::vector<int> disabled;
    for (int i = 0; i < layout.outputBuses.size(); ++i)
        if (layout.outputBuses[i].isDisabled())
            disabled.push_back(i);
    if (disabled.empty())
        return;

    // The whole set at once; a plugin that refuses it gets the buses one at a time, keeping each it accepts.
    auto whole = layout;
    int wholeTotal = total;
    for (int i : disabled) {
        const auto sets = candidateSets(*instance.getBus(false, i));
        if (!sets.empty() && wholeTotal + sets.front().size() <= kMaxPluginOutputChannels) {
            whole.outputBuses.getReference(i) = sets.front();
            wholeTotal += sets.front().size();
        }
    }
    if (whole == layout)
        return;
    if (instance.checkBusesLayoutSupported(whole)) {
        instance.setBusesLayout(whole);
        return;
    }

    auto accepted = layout;
    for (int i : disabled) {
        for (const auto& set : candidateSets(*instance.getBus(false, i))) {
            if (total + set.size() > kMaxPluginOutputChannels)
                continue;
            auto trial = accepted;
            trial.outputBuses.getReference(i) = set;
            if (instance.checkBusesLayoutSupported(trial)) {
                accepted = trial;
                total += set.size();
                break;
            }
        }
    }
    if (!(accepted == layout))
        instance.setBusesLayout(accepted);
}

//==============================================================================
// The model
//==============================================================================

bool HostedPluginModule::isJoined(const OutputBusInfo& bus) const {
    return bus.numChannels == 2 && (bus.busIndex == 0 ? !isDualIO() : bus.joined);
}

HostedPluginModule::OutputBusInfo* HostedPluginModule::findOutputBus(int busIndex) {
    for (auto& bus : outputBuses_)
        if (bus.busIndex == busIndex)
            return &bus;
    return nullptr;
}

void HostedPluginModule::buildOutputBuses(const juce::AudioPluginInstance& instance) {
    outputBuses_.clear();
    int raw = 0;
    for (int i = 0; i < instance.getBusCount(false); ++i) {
        const auto* bus = instance.getBus(false, i);
        if (bus == nullptr || !bus->isEnabled())
            continue;
        OutputBusInfo info;
        info.busIndex = i;
        info.name = bus->getName();
        info.firstRawChannel = raw;
        info.numChannels = bus->getNumberOfChannels();
        info.shown = (i == 0);
        outputBuses_.push_back(info);
        raw += info.numChannels;
    }
    const auto* main = findOutputBus(0);
    mainOutputChannels_.store(main != nullptr ? main->numChannels : 0, std::memory_order_relaxed);
    applyOutputPolicy();
}

// Which aux buses show, in order of authority: the saved "outputs" entry for that bus index (a bus that no longer
// exists is simply never matched); else, for a project saved before outputs were chosen, the buses the instance has on
// by default (before this existed the raw channels WERE the default-enabled buses, so every jack and cable reopens;
// that holds exactly when those buses form a prefix, which is how plugins declare them); else the main bus alone.
bool HostedPluginModule::applyOutputPolicy() {
    bool changed = false;
    const auto* saved = savedOutputs_.getArray();
    const auto isDefaultEnabled = [this](int busIndex) {
        return std::find(defaultEnabledOutputBuses_.begin(), defaultEnabledOutputBuses_.end(), busIndex) !=
               defaultEnabledOutputBuses_.end();
    };
    // An old project only ever had more than the main bus when those default buses fitted the old 16-channel module;
    // a wider plugin (Maschine 2: 32 by default) was cut to its main bus or refused, so it reopens with the main bus.
    int defaultChannels = 0;
    for (const auto& bus : outputBuses_)
        if (isDefaultEnabled(bus.busIndex))
            defaultChannels += bus.numChannels;
    const bool oldProjectShowsDefaults = defaultChannels <= kLegacyMaxPluginChannels;
    for (auto& bus : outputBuses_) {
        if (bus.busIndex == 0)
            continue;
        bool shown = false;
        bool joined = false;
        if (outputsRestored_ && saved != nullptr) {
            for (const auto& entry : *saved)
                if (static_cast<int>(entry.getProperty("bus", -1)) == bus.busIndex) {
                    shown = static_cast<bool>(entry.getProperty("shown", false));
                    joined = static_cast<bool>(entry.getProperty("joined", false)) && bus.numChannels == 2;
                }
        } else if (outputsRestored_) {
            shown = oldProjectShowsDefaults && isDefaultEnabled(bus.busIndex);
        }
        changed |= (bus.shown != shown || bus.joined != joined);
        bus.shown = shown;
        bus.joined = joined;
    }
    return changed;
}

void HostedPluginModule::applySavedOutputs(const juce::var& saved, bool keyPresent) {
    savedOutputs_ = keyPresent ? saved : juce::var();
    outputsRestored_ = true;
    if (!outputBuses_.empty() && applyOutputPolicy() && onOutputsChanged)
        onOutputsChanged();
}

const std::vector<HostedPluginModule::OutputBusInfo>& HostedPluginModule::getOutputBuses() const {
    for (auto& bus : outputBuses_)
        if (bus.busIndex == 0)
            bus.joined = isJoined(bus); // the main bus follows Dual I/O
    return outputBuses_;
}

bool HostedPluginModule::setOutputShown(int busIndex, bool shown) {
    auto* bus = busIndex == 0 ? nullptr : findOutputBus(busIndex);
    if (bus == nullptr || bus->shown == shown)
        return false;
    bus->shown = shown;
    if (onOutputsChanged)
        onOutputsChanged();
    return true;
}

bool HostedPluginModule::setOutputJoined(int busIndex, bool joined) {
    auto* bus = busIndex == 0 ? nullptr : findOutputBus(busIndex);
    if (bus == nullptr || bus->numChannels != 2 || bus->joined == joined)
        return false;
    bus->joined = joined;
    if (onOutputsChanged)
        onOutputsChanged();
    return true;
}

juce::var HostedPluginModule::outputsToVar() const {
    if (outputBuses_.empty())
        return (outputsRestored_ && savedOutputs_.isArray()) ? savedOutputs_ : juce::var(); // a placeholder keeps it

    juce::Array<juce::var> entries;
    for (const auto& bus : getOutputBuses()) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("bus", bus.busIndex);
        entry->setProperty("name", bus.name);
        entry->setProperty("shown", bus.shown);
        entry->setProperty("joined", bus.joined);
        entries.add(juce::var(entry));
    }
    return entries;
}

//==============================================================================
// Jacks
//==============================================================================

int HostedPluginModule::getVisibleOutputPortCount() const {
    if (outputBuses_.empty())
        return visibleOutputs_.load(std::memory_order_relaxed); // bare module (1) or an instance with no outputs (0)
    int count = 0;
    for (const auto& bus : outputBuses_)
        if (bus.shown)
            count += isJoined(bus) ? 1 : bus.numChannels;
    return count;
}

bool HostedPluginModule::locateJack(int jack, std::size_t& busPos, int& leg) const {
    int base = 0;
    for (std::size_t i = 0; i < outputBuses_.size(); ++i) {
        const auto& bus = outputBuses_[i];
        if (!bus.shown)
            continue;
        const int jacks = isJoined(bus) ? 1 : bus.numChannels;
        if (jack < base + jacks) {
            busPos = i;
            leg = jack - base;
            return true;
        }
        base += jacks;
    }
    return false;
}

juce::String HostedPluginModule::getOutputPortLabel(int channelIndex) const {
    std::size_t pos = 0;
    int leg = 0;
    if (outputBuses_.empty() || !locateJack(channelIndex, pos, leg))
        return "Out " + juce::String(channelIndex + 1);

    const auto& bus = outputBuses_[pos];
    const bool joined = isJoined(bus);
    const int raw = bus.firstRawChannel + leg;

    if (bus.busIndex == 0) {
        if (joined)
            return "Audio";
        if (bus.numChannels == 2)
            return leg == 0 ? "Out L" : "Out R";
        return "Out " + juce::String(raw + 1);
    }

    if (isGenericBusName(bus.name)) {
        if (joined)
            return "Out " + juce::String(raw + 1) + "/" + juce::String(raw + 2);
        return "Out " + juce::String(raw + 1);
    }
    if (joined || bus.numChannels == 1)
        return bus.name;
    if (bus.numChannels == 2)
        return bus.name + (leg == 0 ? " L" : " R");
    return bus.name + " " + juce::String(leg + 1);
}

LogicalPort HostedPluginModule::mapOutputChannel(int rawChannel) const {
    LogicalPort port;
    port.role = PortRole::Audio;
    port.polyVoiceSpan = 1;

    if (outputBuses_.empty()) { // bare module, or an instance with no outputs
        const int visible = getVisibleOutputPortCount();
        port.visibleJackIndex = (visible > 0) ? juce::jlimit(0, visible - 1, rawChannel) : 0;
        port.isPolyGroupHead = (rawChannel < visible);
        return port;
    }

    int jackBase = 0;
    for (const auto& bus : outputBuses_) {
        const bool inside = rawChannel >= bus.firstRawChannel && rawChannel < bus.firstRawChannel + bus.numChannels;
        if (!bus.shown) {
            if (inside) { // a hidden channel carries no cable and owns no jack
                port.visibleJackIndex = 0;
                port.isPolyGroupHead = false;
                return port;
            }
            continue;
        }
        const bool joined = isJoined(bus);
        if (inside) {
            const int leg = rawChannel - bus.firstRawChannel;
            port.visibleJackIndex = jackBase + (joined ? 0 : leg);
            port.isPolyGroupHead = !joined || leg == 0; // a joined pair's left leg heads one jack of two legs
            port.polyVoiceSpan = (joined && leg == 0) ? 2 : 1;
            return port;
        }
        jackBase += joined ? 1 : bus.numChannels;
    }
    port.visibleJackIndex = 0;
    port.isPolyGroupHead = false;
    return port;
}

} // namespace synth
