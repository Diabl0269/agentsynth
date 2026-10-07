// AppUndoManager's snapshot steps are sized once, from the lengths the record call's change check measured when it can
// (AppUndoManagerSnapshotSize.cpp): the size is always the states' JSON length, measured or not.
#include "AppUndoManagerSnapshotSize.h"
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

int jsonLength(const juce::var& v) { return juce::JSON::toString(v).length(); }

} // namespace

TEST(UndoSnapshotSizeTest, ASizeIsTheStatesJsonLengthWithOrWithoutAMeasuredCheck) {
    const auto before = state(3), after = state(4);
    const int expected = jsonLength(before) + jsonLength(after);

    int unmeasured = -1;
    EXPECT_EQ(undo_size::sizedOnce(unmeasured, {&before, &after}), expected);

    undo_size::MeasuredJson measured;
    EXPECT_TRUE(measured.differs(before, after));
    EXPECT_EQ(undo_size::MeasuredJson::lengthOf(before), jsonLength(before));
    int fromCheck = -1;
    EXPECT_EQ(undo_size::sizedOnce(fromCheck, {&before, &after}), expected);
}

TEST(UndoSnapshotSizeTest, AStepKeepsTheSizeItFirstTook) {
    const auto before = state(2), after = state(2);
    int memo = -1;
    const int first = undo_size::sizedOnce(memo, {&before, &after});
    EXPECT_EQ(memo, first);
    EXPECT_EQ(undo_size::sizedOnce(memo, {&before, &after}), first);
}

TEST(UndoSnapshotSizeTest, OnlyALiveCheckOffersItsLengthsAndChecksNest) {
    const auto a = state(1), b = state(5);
    EXPECT_EQ(undo_size::MeasuredJson::lengthOf(a), -1) << "no check is live";
    {
        undo_size::MeasuredJson outer;
        EXPECT_FALSE(outer.differs(a, a));
        {
            undo_size::MeasuredJson inner;
            EXPECT_EQ(undo_size::MeasuredJson::lengthOf(a), -1) << "the innermost check is the one asked";
            inner.differs(b, b);
            EXPECT_EQ(undo_size::MeasuredJson::lengthOf(b), jsonLength(b));
        }
        EXPECT_EQ(undo_size::MeasuredJson::lengthOf(a), jsonLength(a)) << "the outer check is live again";
    }
    EXPECT_EQ(undo_size::MeasuredJson::lengthOf(a), -1) << "a finished check offers nothing";
}
