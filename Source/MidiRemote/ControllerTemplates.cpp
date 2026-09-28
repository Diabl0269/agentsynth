#include "MidiRemote/ControllerTemplates.h"

#include "BinaryData.h"
#include "MidiRemote/MidiRemoteMapping.h"

#include <algorithm>
#include <iterator>

namespace synth::midi {

namespace {

const char* const kTemplatePrefix = "template-";
const char* const kTemplateSuffix = ".json";

// Display order of the shipped templates, keyed on id.
const char* const kOrder[] = {"template-8-knobs", "template-8-faders-8-buttons", "template-transport-strip",
                              "template-keyboard-8-knobs"};

int orderIndex(const juce::String& id) {
    for (int i = 0; i < static_cast<int>(std::size(kOrder)); ++i)
        if (id == kOrder[i])
            return i;
    return static_cast<int>(std::size(kOrder));
}

// Parses one BinaryData resource (by its symbol name) into a ControllerProfile. `vendor`/`source`
// are optional out-params: ControllerProfile::fromVar only reads the keys it knows about, so the
// template-only "vendor"/"source" metadata is read straight off the parsed juce::var rather than
// living on ControllerProfile itself (applyControllerTemplate copies Controls only -- template
// metadata must never leak into a user's saved profile).
bool parseResource(const char* resourceName, ControllerProfile& out, juce::String* vendor = nullptr,
                   juce::String* source = nullptr) {
    int size = 0;
    const char* data = BinaryData::getNamedResource(resourceName, size);
    if (data == nullptr || size <= 0)
        return false;
    const juce::var parsed = juce::JSON::parse(juce::String::fromUTF8(data, size));
    ControllerProfile profile;
    if (!profile.fromVar(parsed))
        return false;
    out = std::move(profile);
    if (auto* obj = parsed.getDynamicObject()) {
        if (vendor != nullptr)
            *vendor = obj->getProperty("vendor").toString();
        if (source != nullptr)
            *source = obj->getProperty("source").toString();
    }
    return true;
}

// True for a template resource: original filename "template-*.json".
bool isTemplateResource(const char* resourceName) {
    const char* original = BinaryData::getNamedResourceOriginalFilename(resourceName);
    if (original == nullptr)
        return false;
    const auto filename = juce::String::fromUTF8(original);
    return filename.startsWith(kTemplatePrefix) && filename.endsWith(kTemplateSuffix);
}

bool hasSpec(const std::vector<Control>& controls, const MessageSpec& spec) {
    return std::any_of(controls.begin(), controls.end(), [&](const Control& c) { return c.message == spec; });
}

// Does any assignment in `actions` already claim `spec`? Same "existing wins" dedup rule
// applyControllerTemplate already uses for controls (hasSpec above), applied to profile.actions so
// re-applying a template (or applying two templates that both bind the same physical message) never
// doubles up a transport binding.
bool hasActionSpec(const std::vector<Assignment>& actions, const MessageSpec& spec) {
    return std::any_of(actions.begin(), actions.end(), [&](const Assignment& a) { return a.spec == spec; });
}

const Control* findControlBySpec(const std::vector<Control>& controls, const MessageSpec& spec) {
    const auto it = std::find_if(controls.begin(), controls.end(), [&](const Control& c) { return c.message == spec; });
    return it == controls.end() ? nullptr : &*it;
}

} // namespace

std::vector<TemplateInfo> listControllerTemplates() {
    std::vector<TemplateInfo> result;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const char* resourceName = BinaryData::namedResourceList[i];
        if (!isTemplateResource(resourceName))
            continue;
        ControllerProfile profile;
        juce::String vendor, source;
        if (!parseResource(resourceName, profile, &vendor, &source))
            continue;
        result.push_back({profile.id, profile.name, vendor, source, profile.handshake.port});
    }
    std::stable_sort(result.begin(), result.end(), [](const TemplateInfo& a, const TemplateInfo& b) {
        const int ia = orderIndex(a.id);
        const int ib = orderIndex(b.id);
        if (ia != ib)
            return ia < ib;
        return a.id < b.id;
    });
    return result;
}

std::vector<TemplateGroup> groupControllerTemplatesByVendor(const std::vector<TemplateInfo>& templates) {
    std::vector<TemplateGroup> groups;
    // Generic (vendor == "") first, then one group per vendor in first-seen order below, sorted
    // alphabetically afterwards -- Generic is kept out of that sort so it always leads.
    TemplateGroup generic;
    generic.vendor = juce::String();
    for (const auto& info : templates) {
        if (info.vendor.isEmpty()) {
            generic.templates.push_back(info);
            continue;
        }
        auto it =
            std::find_if(groups.begin(), groups.end(), [&](const TemplateGroup& g) { return g.vendor == info.vendor; });
        if (it == groups.end()) {
            groups.push_back({info.vendor, {info}});
        } else {
            it->templates.push_back(info);
        }
    }
    std::stable_sort(groups.begin(), groups.end(),
                     [](const TemplateGroup& a, const TemplateGroup& b) { return a.vendor < b.vendor; });
    if (!generic.templates.empty())
        groups.insert(groups.begin(), std::move(generic));
    return groups;
}

bool loadControllerTemplate(const juce::String& id, ControllerProfile& out) {
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const char* resourceName = BinaryData::namedResourceList[i];
        if (!isTemplateResource(resourceName))
            continue;
        ControllerProfile profile;
        if (parseResource(resourceName, profile) && profile.id == id) {
            out = std::move(profile);
            return true;
        }
    }
    return false;
}

TemplateApplyResult applyControllerTemplate(ControllerProfile& profile, const ControllerProfile& tmpl) {
    TemplateApplyResult result;

    // A profile that doesn't already declare a handshake picks up the template's -- there is only
    // one per profile, so this is "existing wins" without a per-item dedup.
    if (profile.handshake.isEmpty())
        profile.handshake = tmpl.handshake;

    // An empty profile keeps the template's own layout; otherwise stack the additions below.
    int rowOffset = 0;
    if (!profile.controls.empty()) {
        int maxRow = profile.controls.front().layout.row;
        for (const auto& c : profile.controls)
            maxRow = std::max(maxRow, c.layout.row);
        rowOffset = maxRow + 1;
    }

    for (const auto& tc : tmpl.controls) {
        if (hasSpec(profile.controls, tc.message)) {
            ++result.skippedDuplicates;
            continue;
        }
        Control added = tc;
        added.id = juce::Uuid().toDashedString();
        added.layout.row += rowOffset;
        profile.controls.push_back(std::move(added));
        ++result.added;
    }

    // A template action's spec matches one of tmpl.controls (its author bound it there, e.g.
    // BeatStep's "play" control) -- by now that control has landed on `profile.controls` above,
    // either just-added or a pre-existing match, either way with a real profile-local id.
    // makeAssignmentForControl re-derives the Assignment from THAT control rather than trusting the
    // template's own placeholder control.profileId/controlId.
    for (const auto& ta : tmpl.actions) {
        if (hasActionSpec(profile.actions, ta.spec)) {
            ++result.actionsSkippedDuplicates;
            continue;
        }
        const Control* control = findControlBySpec(profile.controls, ta.spec);
        if (control == nullptr)
            continue; // malformed template: an action with no matching control -- nothing to bind
        profile.actions.push_back(makeAssignmentForControl(profile, *control, ta.target));
        ++result.actionsAdded;
    }

    return result;
}

} // namespace synth::midi
