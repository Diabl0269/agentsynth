// AppUndoManager's snapshot steps are sized once, by counting their states (synth::estimateJsonSize), and take the size
// of anything the graph snapshot cache already counted from it (AppUndoManagerSnapshotSize.cpp).
#include "AppUndoManagerSnapshotSize.h"
#include "AudioEngine/GraphSnapshotCache.h"
#include <gtest/gtest.h>

namespace {

juce::var state(int nodes) {
    juce::Array<juce::var> list;
    for (int i = 0; i < nodes; ++i) {
        juce::DynamicObject::Ptr node = new juce::DynamicObject();
        node->setProperty("id", i);
        node->setProperty("type", "Filter");
        list.add(juce::var(node.get()));
    }
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("nodes", list);
    return juce::var(root.get());
}

} // namespace

TEST(UndoSnapshotSizeTest, ASizeIsTheStatesCountedSize) {
    const auto before = state(3), after = state(4);
    int memo = -1;
    EXPECT_EQ(undo_size::sizedOnce(memo, {&before, &after}),
              synth::estimateJsonSize(before) + synth::estimateJsonSize(after));
    EXPECT_LT(synth::estimateJsonSize(before), synth::estimateJsonSize(after)) << "it grows with what a state holds";
}

TEST(UndoSnapshotSizeTest, AStepKeepsTheSizeItFirstTook) {
    const auto before = state(2), after = state(2);
    int memo = -1;
    const int first = undo_size::sizedOnce(memo, {&before, &after});
    EXPECT_EQ(memo, first);
    EXPECT_EQ(undo_size::sizedOnce(memo, {&before, &after}), first);
}

TEST(UndoSnapshotSizeTest, AKnownObjectIsNotCountedAgainAndScopesNest) {
    const auto s = state(5);
    const auto* firstNode = s["nodes"][0].getDynamicObject();
    int plain = -1;
    const int counted = undo_size::sizedOnce(plain, {&s});
    {
        const undo_size::KnownSizes outer([firstNode](const void* id) { return id == firstNode ? 1000 : -1; });
        {
            const undo_size::KnownSizes inner([](const void*) { return -1; });
            int memo = -1;
            EXPECT_EQ(undo_size::sizedOnce(memo, {&s}), counted) << "the innermost scope is the one asked";
        }
        int memo = -1;
        EXPECT_EQ(undo_size::sizedOnce(memo, {&s}), counted - synth::estimateJsonSize(s["nodes"][0]) + 1000)
            << "the outer scope is live again";
    }
    int memo = -1;
    EXPECT_EQ(undo_size::sizedOnce(memo, {&s}), counted) << "a finished scope offers nothing";
}
