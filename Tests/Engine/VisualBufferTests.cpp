#include "Modules/VisualBuffer.h"
#include <gtest/gtest.h>

TEST(VisualBufferTest, InitialState) {
    VisualBuffer vb(128);
    EXPECT_EQ(vb.getSize(), 128);
}

TEST(VisualBufferTest, PushAndRead) {
    VisualBuffer vb(10);

    // Push some data
    for (int i = 0; i < 5; ++i)
        vb.pushSample((float)i);

    // Read buffer
    std::vector<float> dest(10);
    vb.copyTo(dest);

    // Buffer should contain [0,1,2,3,4, 0,0,0,0,0] potentially rotated depending
    // on implementation. Logic: "Read starting from current write position
    // (oldest sample)" WritePos is at 5. i=0 -> (5+0)%10 = 5 -> buffer[5]=0 i=1
    // -> (5+1)%10 = 6 -> buffer[6]=0
    // ...
    // i=5 -> (5+5)%10 = 0 -> buffer[0]=0
    // Wait, let's trace:
    // push(0) -> buf[0]=0, wp=1
    // push(1) -> buf[1]=1, wp=2
    // ...
    // push(4) -> buf[4]=4, wp=5
    // copyTo: pos=5.
    // dest[0] = buf[(5+0)%10] = buf[5] (0.0)
    // dest[1] = buf[6] (0.0)
    // ...
    // dest[5] = buf[0] (0.0) -- Wait, buf[0] is 0.0 (pushed first).

    // Actually, if we want to see the "waveform", we usually want the latest
    // data? "Read starting from current write position (oldest sample)" means we
    // get the OLDEST sample at index 0. If we pushed 5 samples into size 10
    // initialized to 0. Oldest sample in buffer logic is actually at writePos
    // (since we just overwrote writePos-1). The sample at writePos is the
    // *oldest* valid sample (or empty/zero). If the buffer was full: push 0..9.
    // wp=0. read: i=0 -> buf[0] -> 0. i=9 -> buf[9] -> 9. So distinct order:
    // 0,1,2...9. Correct. 0 is oldest.

    // If not full (zeros):
    // buf=[0,1,2,3,4, 0,0,0,0,0]
    // wp=5.
    // read i=0 -> buf[5] (0)
    // ...
    // read i=5 -> buf[0] (0)
    // ...
    // read i=9 -> buf[4] (4).
    // So dest should be [0,0,0,0,0, 0,1,2,3,4].
    // Let's verify dest[9] == 4.0f.

    EXPECT_EQ(dest[9], 4.0f);
    EXPECT_EQ(dest[8], 3.0f);
    EXPECT_EQ(dest[0], 0.0f);
}

TEST(VisualBufferTest, Overflow) {
    VisualBuffer vb(5);
    // Push 0,1,2,3,4,5,6
    for (int i = 0; i < 7; ++i)
        vb.pushSample((float)i);
    // buf state:
    // i=0->[0,?,?,?,?], wp=1
    // ...
    // i=4->[0,1,2,3,4], wp=0 (wrap)
    // i=5->[5,1,2,3,4], wp=1
    // i=6->[5,6,2,3,4], wp=2

    std::vector<float> dest(5);
    vb.copyTo(dest);

    // Start at wp=2.
    // dest[0]=buf[2]=2
    // dest[1]=buf[3]=3
    // dest[2]=buf[4]=4
    // dest[3]=buf[0]=5
    // dest[4]=buf[1]=6

    EXPECT_EQ(dest[0], 2.0f);
    EXPECT_EQ(dest[4], 6.0f);
}
