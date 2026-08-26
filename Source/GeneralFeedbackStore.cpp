#include "GeneralFeedbackStore.h"
#include "Branding.h"

namespace synth {

namespace {
juce::File defaultFeedbackFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(synth::branding::kSettingsFolderName)
        .getChildFile("general_feedback.jsonl");
}

const char* categoryToString(GeneralFeedbackStore::Category category) {
    switch (category) {
    case GeneralFeedbackStore::Category::Bug:
        return "bug";
    case GeneralFeedbackStore::Category::Feature:
        return "feature";
    case GeneralFeedbackStore::Category::Other:
    default:
        return "other";
    }
}
} // namespace

GeneralFeedbackStore::GeneralFeedbackStore()
    : file(defaultFeedbackFile()) {}

GeneralFeedbackStore::GeneralFeedbackStore(juce::File feedbackFile)
    : file(std::move(feedbackFile)) {}

void GeneralFeedbackStore::record(const juce::String& text, Category category) {
    auto* obj = new juce::DynamicObject();
    juce::var root(obj);
    obj->setProperty("timestamp", juce::Time::getCurrentTime().toISO8601(true));
    obj->setProperty("category", categoryToString(category));
    obj->setProperty("text", text);

    const auto parent = file.getParentDirectory();
    if (!parent.exists())
        parent.createDirectory();

    file.appendText(juce::JSON::toString(root, /*allOnOneLine=*/true) + "\n");
}

} // namespace synth
