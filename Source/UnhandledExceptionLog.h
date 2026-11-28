#pragma once

#include <exception>
#include <juce_core/juce_core.h>

namespace synth {

/** One line describing an exception that escaped a message-thread callback: its `what()` (or "unknown
 *  exception" for a non-std throw) and the JUCE dispatch site that caught it. */
juce::String describeUnhandledException(const std::exception* e, const juce::String& sourceFile, int line);

/** Where the app writes them: `<user data>/logs/unhandled-exceptions.log`. */
juce::File unhandledExceptionLogFile();

/** Writes the description to the JUCE log and appends it, time-stamped, to `logFile` (an empty File skips the
 *  file). Only records: the caller decides what happens next. Never throws. */
void recordUnhandledException(const juce::File& logFile, const std::exception* e, const juce::String& sourceFile,
                              int line) noexcept;

} // namespace synth
