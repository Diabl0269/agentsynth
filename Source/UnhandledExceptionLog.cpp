#include "UnhandledExceptionLog.h"
#include "UserSettings.h"

namespace synth {

juce::String describeUnhandledException(const std::exception* e, const juce::String& sourceFile, int line) {
    const juce::String what = e != nullptr ? juce::String::fromUTF8(e->what()) : juce::String("unknown exception");
    return "Unhandled exception on the message thread: " + what + " (caught at " +
           juce::File(sourceFile).getFileName() + ":" + juce::String(line) + ")";
}

juce::File unhandledExceptionLogFile() {
    return userSettingsRootDirectory().getChildFile("logs").getChildFile("unhandled-exceptions.log");
}

void recordUnhandledException(const juce::File& logFile, const std::exception* e, const juce::String& sourceFile,
                              int line) noexcept {
    try {
        const auto text = describeUnhandledException(e, sourceFile, line);
        juce::Logger::writeToLog(text);
        if (logFile != juce::File()) {
            logFile.getParentDirectory().createDirectory();
            logFile.appendText(juce::Time::getCurrentTime().toISO8601(true) + " " + text + "\n");
        }
    } catch (...) {
    }
}

} // namespace synth
