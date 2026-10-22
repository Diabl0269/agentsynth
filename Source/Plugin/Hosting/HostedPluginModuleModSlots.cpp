// Concern: HostedPluginModule's modulation slots -- mod cables onto the hosted plugin's own parameters.
//
// A plugin parameter has no CV input of its own, so the module carries kMaxModSlots extra input channels above its
// 16 audio ones. A cable dropped on a plugin knob claims a slot for that parameter (bindModSlot) and is wired to the
// slot's channel like any CV jack, so the Mod Matrix, rings and timeline see an ordinary modulation routing.
//
// Every block the audio thread writes base + CV into the parameter (applyModSlots). The base is the resting value:
// whatever the parameter was when the slot was claimed, then whatever anything else sets it to. A write we did not
// make (the plugin's own window, the card knob, an automation lane) shows up as a parameter value different from
// what we read back after our own write, and becomes the new base -- so turning a modulated control anywhere keeps
// working the way it does without a cable.
#include "HostedPluginModule.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {
constexpr const char* kModSlotsKey = "modSlots";
constexpr float kReadBackTolerance = 1.0e-5f;

int slotOf(int rawChannel) noexcept {
    return HostedPluginModule::isModChannel(rawChannel) ? rawChannel - HostedPluginModule::kModChannelBase : -1;
}

// The index of `paramId` on `instance`: its stable id, else the "legacy:<index>" key getInstanceParameters() hands
// out for a parameter with no id. -1 when the instance has no such parameter.
int resolveParamIndex(juce::AudioPluginInstance& instance, const juce::String& paramId) {
    const auto& params = instance.getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(params[i]))
            if (hosted->getParameterID() == paramId)
                return i;
    if (paramId.startsWith("legacy:")) {
        const int index = paramId.fromFirstOccurrenceOf(":", false, false).getIntValue();
        if (index >= 0 && index < params.size())
            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(params[index]);
                hosted == nullptr || hosted->getParameterID().isEmpty())
                return index;
    }
    return -1;
}
} // namespace

int HostedPluginModule::bindModSlot(const juce::String& paramId) {
    if (paramId.isEmpty())
        return -1;
    if (const int existing = modChannelForParam(paramId); existing >= 0)
        return existing;

    int slot = -1;
    for (int k = 0; k < kMaxModSlots && slot < 0; ++k)
        if (modSlotParamIds_[(size_t)k].isEmpty())
            slot = k;
    if (slot < 0)
        return -1;

    modSlotParamIds_[(size_t)slot] = paramId;
    auto* instance = activeInstance_.load(std::memory_order_acquire);
    const int index = instance != nullptr ? resolveParamIndex(*instance, paramId) : -1;
    const float base = index >= 0 ? instance->getParameters()[index]->getValue() : -1.0f;
    modSlotBase_[(size_t)slot].store(base, std::memory_order_relaxed);
    modResync_.fetch_add(1, std::memory_order_release);
    // Last, with release: the audio thread only touches a slot once it sees an index, and by then the base is set.
    modSlotParamIndex_[(size_t)slot].store(index, std::memory_order_release);
    return kModChannelBase + slot;
}

void HostedPluginModule::releaseModSlot(int rawChannel) {
    const int slot = slotOf(rawChannel);
    if (slot < 0 || modSlotParamIds_[(size_t)slot].isEmpty())
        return;

    const int index = modSlotParamIndex_[(size_t)slot].exchange(-1, std::memory_order_acq_rel);
    const float base = modSlotBase_[(size_t)slot].load(std::memory_order_relaxed);
    modSlotParamIds_[(size_t)slot].clear();

    // Back to the resting value, so removing the last cable never leaves the parameter wherever the LFO had it.
    // A block already in flight may still write base + CV once; with the cable gone its CV reads 0, which is base.
    if (auto* instance = activeInstance_.load(std::memory_order_acquire);
        instance != nullptr && index >= 0 && base >= 0)
        if (index < instance->getParameters().size())
            instance->getParameters()[index]->setValue(base);
}

void HostedPluginModule::releaseUnconnectedModSlots(const std::function<bool(int rawChannel)>& isConnected) {
    for (int k = 0; k < kMaxModSlots; ++k)
        if (modSlotParamIds_[(size_t)k].isNotEmpty() && !isConnected(kModChannelBase + k))
            releaseModSlot(kModChannelBase + k);
}

int HostedPluginModule::modChannelForParam(const juce::String& paramId) const {
    if (paramId.isEmpty())
        return -1;
    for (int k = 0; k < kMaxModSlots; ++k)
        if (modSlotParamIds_[(size_t)k] == paramId)
            return kModChannelBase + k;
    return -1;
}

bool HostedPluginModule::canModulateParam(const juce::String& paramId) const {
    if (modChannelForParam(paramId) >= 0)
        return true;
    auto* instance = activeInstance_.load(std::memory_order_acquire);
    if (paramId.isEmpty() || instance == nullptr || resolveParamIndex(*instance, paramId) < 0)
        return false;
    return std::any_of(modSlotParamIds_.begin(), modSlotParamIds_.end(), [](const auto& id) { return id.isEmpty(); });
}

juce::String HostedPluginModule::modSlotParamId(int rawChannel) const {
    const int slot = slotOf(rawChannel);
    return slot >= 0 ? modSlotParamIds_[(size_t)slot] : juce::String();
}

std::optional<float> HostedPluginModule::modSlotBase(const juce::String& paramId) const {
    const int slot = slotOf(modChannelForParam(paramId));
    if (slot < 0)
        return std::nullopt;
    const float base = modSlotBase_[(size_t)slot].load(std::memory_order_relaxed);
    if (base < 0.0f)
        return std::nullopt;
    return base;
}

std::vector<ModulationTarget> HostedPluginModule::getModulationTargets() const {
    std::vector<ModulationTarget> targets;
    auto* instance = activeInstance_.load(std::memory_order_acquire);
    for (int k = 0; k < kMaxModSlots; ++k) {
        const auto& paramId = modSlotParamIds_[(size_t)k];
        if (paramId.isEmpty())
            continue;
        juce::String name = paramId;
        const int index = modSlotParamIndex_[(size_t)k].load(std::memory_order_acquire);
        if (instance != nullptr && index >= 0 && index < instance->getParameters().size())
            name = instance->getParameters()[index]->getName(64);
        targets.push_back({name, kModChannelBase + k, paramId, /*knobOnly*/ true});
    }
    return targets;
}

// Runs inside publishInstance, after the old instance was retired and before the new one is published, so the audio
// thread never pairs an index with the wrong instance. A base still unknown (the slot was claimed with no instance)
// is read now; a parameter the new instance lacks leaves its slot unresolved, and the owner is told so it can remove
// the cables (a plugin swapped for another, or an update that dropped a parameter).
void HostedPluginModule::resolveModSlots(juce::AudioPluginInstance& instance) {
    juce::StringArray missing;
    for (int k = 0; k < kMaxModSlots; ++k) {
        const auto& paramId = modSlotParamIds_[(size_t)k];
        if (paramId.isEmpty()) {
            modSlotParamIndex_[(size_t)k].store(-1, std::memory_order_release);
            continue;
        }
        const int index = resolveParamIndex(instance, paramId);
        if (index < 0)
            missing.add(paramId);
        else if (modSlotBase_[(size_t)k].load(std::memory_order_relaxed) < 0.0f)
            modSlotBase_[(size_t)k].store(instance.getParameters()[index]->getValue(), std::memory_order_relaxed);
        modSlotParamIndex_[(size_t)k].store(index, std::memory_order_release);
    }
    modResync_.fetch_add(1, std::memory_order_release);

    if (!missing.isEmpty() && onModSlotsUnresolved) {
        // After the publish completes: the owner removes cables, which must not run inside publishInstance.
        juce::WeakReference<HostedPluginModule> self(this);
        juce::MessageManager::callAsync([self, missing] {
            if (auto* module = self.get(); module != nullptr && module->onModSlotsUnresolved)
                module->onModSlotsUnresolved(missing);
        });
    }
}

// Audio thread, once per block before the plugin renders. No allocation, no locks: indices and bases are atomics,
// read-backs are this thread's own. A read-back is NaN until we have written the slot once since the last resync,
// so a parameter that moved while we were not watching (a state restore, a fresh instance) is never mistaken for
// the user turning it.
void HostedPluginModule::applyModSlots(juce::AudioPluginInstance& instance,
                                       const juce::AudioBuffer<float>& buffer) noexcept {
    if (const auto resync = modResync_.load(std::memory_order_acquire); resync != modResyncSeen_) {
        modSlotReadBack_.fill(std::numeric_limits<float>::quiet_NaN());
        modResyncSeen_ = resync;
    }

    const auto& params = instance.getParameters();
    for (int k = 0; k < kMaxModSlots; ++k) {
        const int index = modSlotParamIndex_[(size_t)k].load(std::memory_order_acquire);
        if (index < 0 || index >= params.size())
            continue;
        auto* param = params[index];
        float base = modSlotBase_[(size_t)k].load(std::memory_order_relaxed);
        const float current = param->getValue();
        const float readBack = modSlotReadBack_[(size_t)k];
        if (base < 0.0f || (!std::isnan(readBack) && std::abs(current - readBack) > kReadBackTolerance)) {
            base = current; // changed by something other than us: that is the new resting value
            modSlotBase_[(size_t)k].store(base, std::memory_order_relaxed);
        }
        const float target = juce::jlimit(0.0f, 1.0f, base + blockCV(buffer, kModChannelBase + k));
        if (target != current)
            param->setValue(target);
        modSlotReadBack_[(size_t)k] = param->getValue();
    }
}

juce::var HostedPluginModule::modSlotsToVar() const {
    juce::Array<juce::var> slots;
    for (int k = 0; k < kMaxModSlots; ++k) {
        if (modSlotParamIds_[(size_t)k].isEmpty())
            continue;
        auto* entry = new juce::DynamicObject();
        entry->setProperty("slot", k);
        entry->setProperty("paramId", modSlotParamIds_[(size_t)k]);
        const float base = modSlotBase_[(size_t)k].load(std::memory_order_relaxed);
        if (base >= 0.0f)
            entry->setProperty("base", base);
        slots.add(juce::var(entry));
    }
    return slots.isEmpty() ? juce::var() : juce::var(slots);
}

// Replaces the whole table. Indices are resolved against the live instance when there is one (an in-place restore),
// else at the next publish. A slot number out of range or a duplicate is dropped: the file is ours, but not trusted
// to be well formed.
void HostedPluginModule::modSlotsFromVar(const juce::var& slots) {
    std::array<juce::String, kMaxModSlots> ids;
    std::array<float, kMaxModSlots> bases;
    bases.fill(-1.0f);
    if (auto* array = slots.getArray()) {
        for (const auto& entry : *array) {
            const int slot = entry.getProperty("slot", -1);
            const juce::String paramId = entry.getProperty("paramId", {}).toString();
            if (slot < 0 || slot >= kMaxModSlots || paramId.isEmpty() || ids[(size_t)slot].isNotEmpty() ||
                std::find(ids.begin(), ids.end(), paramId) != ids.end())
                continue;
            ids[(size_t)slot] = paramId;
            if (entry.hasProperty("base"))
                bases[(size_t)slot] = juce::jlimit(0.0f, 1.0f, (float)entry.getProperty("base", 0.0));
        }
    }

    auto* instance = activeInstance_.load(std::memory_order_acquire);
    for (int k = 0; k < kMaxModSlots; ++k) {
        // A slot whose parameter changed stops first, so the audio thread never writes the new base to the old one.
        if (modSlotParamIds_[(size_t)k] != ids[(size_t)k])
            modSlotParamIndex_[(size_t)k].store(-1, std::memory_order_release);
        modSlotParamIds_[(size_t)k] = ids[(size_t)k];
        modSlotBase_[(size_t)k].store(bases[(size_t)k], std::memory_order_relaxed);
    }
    modResync_.fetch_add(1, std::memory_order_release);
    if (instance == nullptr)
        return;
    for (int k = 0; k < kMaxModSlots; ++k) {
        const int index = ids[(size_t)k].isEmpty() ? -1 : resolveParamIndex(*instance, ids[(size_t)k]);
        if (index >= 0 && bases[(size_t)k] < 0.0f)
            modSlotBase_[(size_t)k].store(instance->getParameters()[index]->getValue(), std::memory_order_relaxed);
        modSlotParamIndex_[(size_t)k].store(index, std::memory_order_release);
    }
}

// Undo and redo replay a node's whole saved state. Reloading the plugin for that would close its window, glitch the
// audio and, for a modulated parameter, always happen (its value in the plugin's own state moves every block). When
// the identity is the plugin already live, the state goes into that instance instead: the card layout, the slot
// table, and the plugin's own bytes only when they differ from what it holds now.
bool HostedPluginModule::applyStateInPlace(const PluginIdentity& identity, const juce::DynamicObject& object) {
    auto* instance = activeInstance_.load(std::memory_order_acquire);
    if (instance == nullptr || loading_ || !identity.isValid() || !(identity == identity_))
        return false;

    setCardLayoutOverride(object.getProperty("cardLayout"));

    const juce::String encoded = object.getProperty("pluginState").toString();
    if (encoded.isNotEmpty()) {
        juce::MemoryBlock target;
        target.fromBase64Encoding(encoded);
        juce::MemoryBlock current;
        instance->getStateInformation(current);
        if (target != current)
            instance->setStateInformation(target.getData(), static_cast<int>(target.getSize()));
    }

    // After the plugin's bytes: the slot bases are the resting values, and the restore may have set a parameter to a
    // value it only had because it was being modulated when that state was saved.
    modSlotsFromVar(object.getProperty(kModSlotsKey));
    return true;
}

} // namespace synth
