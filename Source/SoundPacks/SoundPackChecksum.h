#pragma once

// SHA-256 of a downloaded pack, so a corrupt or swapped file is rejected before anything is unpacked.

#include <atomic>
#include <juce_core/juce_core.h>

namespace synth::soundpacks {

/** Lower-case hex SHA-256 of `size` bytes. */
juce::String sha256Hex(const void* data, size_t size);

/** Lower-case hex SHA-256 of a file read in chunks; empty when the file cannot be read or `cancel` turns true. */
juce::String sha256Hex(const juce::File& file, const std::atomic<bool>& cancel);

} // namespace synth::soundpacks
