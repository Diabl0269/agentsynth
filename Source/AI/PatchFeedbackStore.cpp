#include "PatchFeedbackStore.h"
#include "../Branding.h"

namespace synth {

namespace {
juce::File defaultFeedbackFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(synth::branding::kSettingsFolderName)
        .getChildFile("patch_feedback.jsonl");
}
} // namespace

PatchFeedbackStore::PatchFeedbackStore()
    : file(defaultFeedbackFile()) {}

PatchFeedbackStore::PatchFeedbackStore(juce::File feedbackFile)
    : file(std::move(feedbackFile)) {}

void PatchFeedbackStore::record(const juce::String& patchJson, Rating rating, const juce::String& comment) {
    auto* obj = new juce::DynamicObject();
    juce::var root(obj);
    obj->setProperty("timestamp", juce::Time::getCurrentTime().toISO8601(true));
    obj->setProperty("rating", rating == Rating::Up ? "up" : "down");
    if (comment.isNotEmpty())
        obj->setProperty("comment", comment);

    juce::var parsedPatch = juce::JSON::parse(patchJson);
    if (!parsedPatch.isVoid())
        obj->setProperty("patch", parsedPatch);
    else
        obj->setProperty("patchRaw", patchJson);

    const auto parent = file.getParentDirectory();
    if (!parent.exists())
        parent.createDirectory();

    file.appendText(juce::JSON::toString(root, /*allOnOneLine=*/true) + "\n");
}

} // namespace synth
