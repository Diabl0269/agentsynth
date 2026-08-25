// Unit coverage for synth::harness::RequestBudget (P1-11) — the hard ceiling AIPatchHarness's
// RecordingProvider enforces on outbound model requests so a nightly/scheduled run (potentially
// against a paid vendor via --provider remote) cannot run away.

#include "../Tools/AIPatchHarness/RequestBudget.h"
#include <gtest/gtest.h>

using synth::harness::RequestBudget;

TEST(RequestBudgetTests, UnlimitedWhenLimitIsZeroOrNegative) {
    RequestBudget zero(0);
    RequestBudget negative(-1);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(zero.tryConsume());
        EXPECT_TRUE(negative.tryConsume());
    }
}

TEST(RequestBudgetTests, RefusesPastTheLimit) {
    RequestBudget budget(3);
    EXPECT_TRUE(budget.tryConsume());
    EXPECT_TRUE(budget.tryConsume());
    EXPECT_TRUE(budget.tryConsume());
    EXPECT_FALSE(budget.tryConsume());
    EXPECT_FALSE(budget.tryConsume()); // stays refused, not a one-shot trip
}

TEST(RequestBudgetTests, ReportsUsedAndLimit) {
    RequestBudget budget(2);
    EXPECT_EQ(budget.limit(), 2);
    EXPECT_EQ(budget.used(), 0);
    budget.tryConsume();
    EXPECT_EQ(budget.used(), 1);
    budget.tryConsume();
    budget.tryConsume(); // refused, but still counted so the report shows the overrun
    EXPECT_EQ(budget.used(), 3);
}

TEST(RequestBudgetTests, LimitOfOneAllowsExactlyOneRequest) {
    RequestBudget budget(1);
    EXPECT_TRUE(budget.tryConsume());
    EXPECT_FALSE(budget.tryConsume());
}
