// PartitionedConvolver: the zero-latency head + partitioned tail must equal direct-form FIR
// convolution sample for sample, at any block size, and a kernel swap must land exactly on the
// new kernel once its crossfade ends.
#include "AllocationCounter.h"
#include "Modules/Filter/PartitionedConvolver.h"
#include <gtest/gtest.h>
#include <random>
#include <vector>

using synth::PartitionedConvolver;

namespace {

std::vector<float> randomSignal(int n, unsigned seed, float scale = 1.0f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-scale, scale);
    std::vector<float> v((size_t)n);
    for (auto& x : v)
        x = d(rng);
    return v;
}

std::vector<float> directConvolve(const std::vector<float>& x, const std::vector<float>& h) {
    std::vector<float> y(x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n) {
        double acc = 0.0;
        for (size_t t = 0; t < h.size() && t <= n; ++t)
            acc += (double)h[t] * (double)x[n - t];
        y[n] = (float)acc;
    }
    return y;
}

/** Runs `x` through `conv` channel 0 in blocks cycling through `blocks`. */
std::vector<float> runBlocks(PartitionedConvolver& conv, std::vector<float> x, std::initializer_list<int> blocks) {
    std::vector<int> sizes(blocks);
    size_t start = 0;
    for (size_t b = 0; start < x.size(); ++b) {
        const int n = (int)std::min<size_t>((size_t)sizes[b % sizes.size()], x.size() - start);
        float* ch[1] = {x.data() + start};
        conv.process(ch, 1, n);
        start += (size_t)n;
    }
    return x;
}

} // namespace

TEST(PartitionedConvolver, MatchesDirectConvolutionAtAnyBlockSize) {
    for (int kernelLength : {2048, 300, 64}) {
        const auto h = randomSignal(kernelLength, 1u + (unsigned)kernelLength, 0.05f);
        const auto x = randomSignal(6000, 7u);
        const auto expected = directConvolve(x, h);

        PartitionedConvolver conv;
        conv.prepare(kernelLength, 1);
        conv.setKernel(h.data(), 512);
        const auto y = runBlocks(conv, x, {1, 17, 64, 100, 512, 3});
        for (size_t n = 0; n < y.size(); ++n)
            ASSERT_NEAR(y[n], expected[n], 1.0e-4f) << "kernel " << kernelLength << " sample " << n;
    }
}

TEST(PartitionedConvolver, NoLatencyImpulseReturnsKernel) {
    const auto h = randomSignal(2048, 3u, 0.1f);
    PartitionedConvolver conv;
    conv.prepare(2048, 1);
    conv.setKernel(h.data(), 512);
    std::vector<float> x(4096, 0.0f);
    x[0] = 1.0f;
    const auto y = runBlocks(conv, x, {256});
    for (size_t n = 0; n < h.size(); ++n)
        ASSERT_NEAR(y[n], h[n], 1.0e-5f) << n;
    for (size_t n = h.size(); n < y.size(); ++n)
        ASSERT_NEAR(y[n], 0.0f, 1.0e-5f) << n;
}

TEST(PartitionedConvolver, SwapCrossfadesThenMatchesNewKernelExactly) {
    const auto a = randomSignal(2048, 11u, 0.05f);
    const auto b = randomSignal(2048, 12u, 0.05f);
    const auto x = randomSignal(8192, 13u);
    const auto withA = directConvolve(x, a);
    const auto withB = directConvolve(x, b);

    PartitionedConvolver conv;
    conv.prepare(2048, 1);
    conv.setKernel(a.data(), 512);
    std::vector<float> y = x;
    constexpr int kBlock = 512;
    constexpr int kSwapBlock = 6;
    for (int blk = 0; blk * kBlock < (int)y.size(); ++blk) {
        if (blk == kSwapBlock) {
            ASSERT_FALSE(conv.isCrossfading());
            conv.setKernel(b.data(), kBlock);
            EXPECT_TRUE(conv.isCrossfading());
        }
        float* ch[1] = {y.data() + blk * kBlock};
        conv.process(ch, 1, kBlock);
    }
    EXPECT_FALSE(conv.isCrossfading()) << "a one-block fade ends with its block";

    const int swapAt = kSwapBlock * kBlock;
    for (int n = 0; n < swapAt; ++n)
        ASSERT_NEAR(y[(size_t)n], withA[(size_t)n], 1.0e-4f) << n;
    for (int n = swapAt; n < swapAt + kBlock; ++n) {
        const float g = (float)(n - swapAt + 1) / (float)kBlock;
        ASSERT_NEAR(y[(size_t)n], withA[(size_t)n] + (withB[(size_t)n] - withA[(size_t)n]) * g, 1.0e-4f) << n;
    }
    for (int n = swapAt + kBlock; n < (int)y.size(); ++n)
        ASSERT_NEAR(y[(size_t)n], withB[(size_t)n], 1.0e-4f) << n;
}

TEST(PartitionedConvolver, ChannelsKeepSeparateHistory) {
    const auto h = randomSignal(512, 21u, 0.1f);
    const auto x0 = randomSignal(3000, 22u);
    const auto x1 = randomSignal(3000, 23u);
    PartitionedConvolver conv;
    conv.prepare(512, 2);
    conv.setKernel(h.data(), 64);
    auto y0 = x0;
    auto y1 = x1;
    for (int start = 0; start < 3000; start += 300) {
        float* ch[2] = {y0.data() + start, y1.data() + start};
        conv.process(ch, 2, 300);
    }
    const auto e0 = directConvolve(x0, h);
    const auto e1 = directConvolve(x1, h);
    for (size_t n = 0; n < y0.size(); ++n) {
        ASSERT_NEAR(y0[n], e0[n], 1.0e-4f) << n;
        ASSERT_NEAR(y1[n], e1[n], 1.0e-4f) << n;
    }
}

TEST(PartitionedConvolver, ProcessAndSwapDoNotAllocate) {
    const auto a = randomSignal(2048, 31u, 0.05f);
    const auto b = randomSignal(2048, 32u, 0.05f);
    auto x = randomSignal(512 * 16, 33u);
    PartitionedConvolver conv;
    conv.prepare(2048, 16);
    conv.setKernel(a.data(), 512);
    synth::test::AllocationScope scope;
    for (int blk = 0; blk < 16; ++blk) {
        if (blk % 2 == 1)
            conv.setKernel((blk % 4 == 1) ? b.data() : a.data(), 512);
        float* ch[1] = {x.data() + blk * 512};
        conv.process(ch, 1, 512);
    }
    EXPECT_EQ(scope.count(), 0);
}

TEST(PartitionedConvolver, ChannelGroupsReadTheirOwnKernels) {
    const auto x = randomSignal(3000, 21);
    const auto h0 = randomSignal(700, 22, 0.1f);
    const auto h1 = randomSignal(700, 23, 0.1f);
    PartitionedConvolver conv;
    conv.prepare(700, 2, 3);
    conv.setKernel(0, h0.data(), 0);
    conv.setKernel(2, h1.data(), 0);
    conv.setChannelGroup(1, 2);
    EXPECT_EQ(conv.getChannelGroup(0), 0);
    EXPECT_EQ(conv.getChannelGroup(1), 2);
    auto a = x, b = x;
    for (size_t start = 0; start < x.size(); start += 300) {
        float* ch[2] = {a.data() + start, b.data() + start};
        conv.process(ch, 2, 300);
    }
    const auto ya = directConvolve(x, h0), yb = directConvolve(x, h1);
    for (size_t n = 0; n < x.size(); ++n) {
        ASSERT_NEAR(a[n], ya[n], 1e-4f) << n;
        ASSERT_NEAR(b[n], yb[n], 1e-4f) << n;
    }
}

TEST(PartitionedConvolver, MovingAChannelOntoACopiedGroupIsSeamless) {
    const auto x = randomSignal(4000, 31);
    const auto h = randomSignal(500, 32, 0.1f);
    PartitionedConvolver conv;
    conv.prepare(500, 1, 2);
    conv.setKernel(h.data(), 0);
    auto y = x;
    for (size_t start = 0; start < x.size(); start += 250) {
        if (start == 2000) { // mid-stream and mid sub-block (2000 is not a multiple of 64)
            conv.copyLiveKernel(0, 1);
            conv.setChannelGroup(0, 1);
            EXPECT_FALSE(conv.isCrossfading(1));
        }
        float* ch[1] = {y.data() + start};
        conv.process(ch, 1, 250);
    }
    const auto ref = directConvolve(x, h);
    for (size_t n = 0; n < x.size(); ++n)
        ASSERT_NEAR(y[n], ref[n], 1e-4f) << n;
}

TEST(PartitionedConvolver, AGroupSwapLeavesOtherGroupsAlone) {
    const auto x = randomSignal(2048, 41);
    const auto h = randomSignal(300, 42, 0.1f);
    const auto other = randomSignal(300, 43, 0.1f);
    PartitionedConvolver conv;
    conv.prepare(300, 2, 2);
    conv.setKernel(0, h.data(), 0);
    conv.setKernel(1, h.data(), 0);
    conv.setChannelGroup(1, 1);
    auto a = x, b = x;
    for (size_t start = 0; start < x.size(); start += 256) {
        if (start == 1024)
            conv.setKernel(1, other.data(), 256);
        EXPECT_FALSE(conv.isCrossfading(0));
        float* ch[2] = {a.data() + start, b.data() + start};
        conv.process(ch, 2, 256);
    }
    const auto ref = directConvolve(x, h);
    for (size_t n = 0; n < x.size(); ++n)
        ASSERT_NEAR(a[n], ref[n], 1e-4f) << "group 0 never fades: " << n;
    EXPECT_GT(std::abs(b[2000] - ref[2000]), 1e-4f) << "group 1 took the new kernel";
}
