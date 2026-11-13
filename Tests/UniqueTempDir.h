#pragma once

// UniqueTempDir.h -- the one way a test names a scratch folder or file under the system temp
// directory (header-only, included relatively like TestSettingsHelpers.h).
//
// Regression test for FRO1120: concurrent test shards shared one temp folder. CI runs the Tests
// binary as several shard processes at once; JUCE's getNonexistentChildFile only picks a free name
// without reserving it, so two shards started in the same moment could pick the same fixed name,
// and one shard's teardown then deleted the other's project mid-test. Every name here carries the
// process id and a fresh Uuid, and a directory is created before it is returned, so two shards
// (or two calls) can never end up sharing one. UniqueTempDirTests.cpp fails the build of any new
// test that goes back to the fixed-name call.

#include <juce_core/juce_core.h>

#include <gtest/gtest.h>
#if JUCE_WINDOWS
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace synth::test {

/** A path under the temp directory that no other process or call will pick; nothing is created.
 *  For tests that hand the path to code that creates it itself (a project bundle, a wav). */
inline juce::File uniqueTempPath(const juce::String& base, const juce::String& extension = {}) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile(base + "_" + juce::String((int)getpid()) + "_" + juce::Uuid().toString() + extension);
}

/** uniqueTempPath() created as a directory right away; reports a test failure if that fails. */
inline juce::File makeUniqueTempDir(const juce::String& base, const juce::String& extension = {}) {
    const auto dir = uniqueTempPath(base, extension);
    EXPECT_TRUE(dir.createDirectory()) << "could not create " << dir.getFullPathName();
    return dir;
}

} // namespace synth::test
