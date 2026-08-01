#include "SettingsMigration.h"

namespace synth {

namespace {

bool folderHasData(const juce::File& folder) {
    return folder.isDirectory() && folder.getNumberOfChildFiles(juce::File::findFilesAndDirectories) > 0;
}

int copyRecursively(const juce::File& source, const juce::File& destination) {
    int filesCopied = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(source, true, "*", juce::File::findFilesAndDirectories)) {
        const juce::File src = entry.getFile();
        const juce::File dest = destination.getChildFile(src.getRelativePathFrom(source));
        if (src.isDirectory()) {
            dest.createDirectory();
        } else {
            dest.getParentDirectory().createDirectory();
            if (src.copyFileTo(dest))
                ++filesCopied;
        }
    }
    return filesCopied;
}

} // namespace

MigrationResult migrateUserData(const juce::File& parentDir, const juce::String& currentName,
                                const juce::StringArray& legacyNames) {
    const auto currentDir = parentDir.getChildFile(currentName);
    if (folderHasData(currentDir))
        return {false, {}, 0};

    for (const auto& legacyName : legacyNames) {
        const auto legacyDir = parentDir.getChildFile(legacyName);
        if (!folderHasData(legacyDir))
            continue;

        currentDir.createDirectory();
        const int filesCopied = copyRecursively(legacyDir, currentDir);
        return {true, legacyName, filesCopied};
    }

    return {false, {}, 0};
}

} // namespace synth
