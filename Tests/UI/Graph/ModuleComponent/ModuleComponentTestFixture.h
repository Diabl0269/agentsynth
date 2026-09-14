#pragma once

// Shared fixture for the ModuleComponent test suite (Tests/UI/Graph/ModuleComponent/ModuleComponent*Tests.cpp).
// Header-only; not compiled on its own and not registered in Tests/CMakeLists.txt.

#include <gtest/gtest.h>

class ModuleComponentTest : public ::testing::Test {
protected:
};
