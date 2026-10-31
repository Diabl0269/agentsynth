#include "ModCurvePresetStore.h"

#include "UserSettings.h"
#include <algorithm>

namespace synth::ui {

ModCurvePresetStore::ModCurvePresetStore(juce::File folder)
    : file_(folder.getChildFile("mod-curve-presets.json")) {}

ModCurvePresetStore ModCurvePresetStore::app() { return ModCurvePresetStore(synth::userSettingsRootDirectory()); }

std::vector<ModCurvePresetStore::Entry> ModCurvePresetStore::builtIns() {
    using P = synth::ModCurve::Preset;
    return {{"Linear", synth::ModCurve::preset(P::Linear), true},
            {"Exp", synth::ModCurve::preset(P::Exp), true},
            {"Log", synth::ModCurve::preset(P::Log), true},
            {"S-curve", synth::ModCurve::preset(P::SCurve), true}};
}

std::vector<ModCurvePresetStore::Entry> ModCurvePresetStore::userPresets() const {
    std::vector<Entry> out;
    if (!file_.existsAsFile())
        return out;
    const auto parsed = juce::JSON::parse(file_.loadFileAsString());
    const auto* items = parsed.getProperty("presets", juce::var()).getArray();
    if (items == nullptr)
        return out;
    for (const auto& item : *items) {
        const auto name = item.getProperty("name", juce::var()).toString().trim();
        if (name.isEmpty() || !item.hasProperty("curve"))
            continue;
        const bool clash = std::any_of(out.begin(), out.end(), [&name](const Entry& e) { return e.name == name; });
        if (!clash)
            out.push_back({name, synth::ModCurve::fromVar(item["curve"]), false});
    }
    return out;
}

std::vector<ModCurvePresetStore::Entry> ModCurvePresetStore::list() const {
    auto all = builtIns();
    for (auto& e : userPresets()) {
        const bool clash = std::any_of(all.begin(), all.end(), [&e](const Entry& b) { return b.name == e.name; });
        if (!clash)
            all.push_back(std::move(e));
    }
    return all;
}

bool ModCurvePresetStore::write(const std::vector<Entry>& user) const {
    juce::Array<juce::var> items;
    for (const auto& e : user) {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("name", e.name);
        obj->setProperty("curve", e.curve.toVar());
        items.add(juce::var(obj));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty("presets", items);
    if (!file_.getParentDirectory().createDirectory())
        return false;
    return file_.replaceWithText(juce::JSON::toString(juce::var(root)));
}

bool ModCurvePresetStore::save(const juce::String& rawName, const synth::ModCurve& curve) const {
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;
    for (const auto& b : builtIns())
        if (b.name.equalsIgnoreCase(name))
            return false;
    auto user = userPresets();
    const auto it = std::find_if(user.begin(), user.end(), [&name](const Entry& e) { return e.name == name; });
    if (it != user.end())
        it->curve = curve;
    else
        user.push_back({name, curve, false});
    return write(user);
}

bool ModCurvePresetStore::remove(const juce::String& name) const {
    auto user = userPresets();
    const auto before = user.size();
    user.erase(std::remove_if(user.begin(), user.end(), [&name](const Entry& e) { return e.name == name; }),
               user.end());
    return user.size() != before && write(user);
}

} // namespace synth::ui
