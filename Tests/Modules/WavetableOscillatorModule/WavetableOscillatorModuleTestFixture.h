// WavetableOscillatorModuleTestFixture.h
// Shared gtest fixture for the WavetableOscillatorModule test suites.
#pragma once

#include "WavetableOscillatorModuleTestHelpers.h"

class WavetableOscillatorModuleTest : public ::testing::Test {
protected:
    std::unique_ptr<WavetableOscillatorModule> module;

    void SetUp() override {
        module = std::make_unique<WavetableOscillatorModule>();
        module->prepareToPlay(kSampleRate, kBlockSize);
    }
};
