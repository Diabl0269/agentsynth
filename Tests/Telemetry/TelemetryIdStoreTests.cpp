#include "TelemetryTestFixture.h"

using synth::telemetry::TelemetryIdStore;
using synth::telemetry::test::TelemetryTestFixture;

using TelemetryIdStoreTest = TelemetryTestFixture;

TEST_F(TelemetryIdStoreTest, LoadIsEmptyWhenThereIsNoFile) {
    TelemetryIdStore store{idFile()};
    EXPECT_TRUE(store.load().isEmpty());
    EXPECT_FALSE(idFile().existsAsFile());
}

TEST_F(TelemetryIdStoreTest, CreateWritesADashedUuidThatLoadReturns) {
    TelemetryIdStore store{idFile()};
    const auto id = store.create();
    EXPECT_EQ(id.length(), 36);
    EXPECT_EQ(id[14], '4'); // UUID version 4
    EXPECT_EQ(store.load(), id);
    EXPECT_EQ(TelemetryIdStore{idFile()}.load(), id);
}

TEST_F(TelemetryIdStoreTest, EachCreateMakesADifferentId) {
    TelemetryIdStore store{idFile()};
    EXPECT_NE(store.create(), store.create());
}

TEST_F(TelemetryIdStoreTest, EraseDeletesTheFileAndIsSafeTwice) {
    TelemetryIdStore store{idFile()};
    store.create();
    store.erase();
    EXPECT_FALSE(idFile().existsAsFile());
    EXPECT_TRUE(store.load().isEmpty());
    store.erase();
}

TEST_F(TelemetryIdStoreTest, ImplausibleContentsReadAsAbsent) {
    TelemetryIdStore store{idFile()};
    for (const char* junk :
         {"", "not-an-id", "1234", "zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz", "0123456789abcdef0123456789abcdef0123"}) {
        idFile().replaceWithText(junk);
        EXPECT_TRUE(store.load().isEmpty()) << junk;
    }
}

TEST_F(TelemetryIdStoreTest, CreateMakesMissingParentFolders) {
    TelemetryIdStore store{dir.getChildFile("a").getChildFile("b").getChildFile("telemetry_id")};
    EXPECT_TRUE(store.create().isNotEmpty());
    EXPECT_TRUE(store.getFile().existsAsFile());
}
