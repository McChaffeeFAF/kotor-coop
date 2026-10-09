/* Copyright (c) 2026 The reone project contributors */

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include "reone/game/cooploot.h"

using namespace reone::game;

namespace {

CoopLootLedger rich() {
    CoopLootLedger ledger;
    ledger.capture("tar_m02aa", "chest@3,-4,0",
                   {{"g_w_blstrpstl01", "", 1, false, false}, {"geno_datapad", "datapad", 1, true, false}, {"", "", 250, false, true}});
    ledger.markTaken("tar_m02aa", "chest@3,-4,0", "guest-one", 0);
    ledger.markTaken("tar_m02aa", "chest@3,-4,0", "guest-one", 1);
    ledger.markTaken("tar_m02aa", "chest@3,-4,0", "guest-two", 2);
    ledger.capture("tar_m02aa", "body@-1,0,0", {});
    ledger.capture("tar_m03aa", "chest@3,-4,0", {{"g_i_adrnaline001", "", 3, false, false}});
    return ledger;
}

bool parse(const std::string &text, CoopLootLedger &ledger, std::string &error) {
    return ledger.deserialize(text, error);
}

} // namespace

TEST(CoopLootLedgerSave, round_trips_everything_the_ledger_keeps) {
    CoopLootLedger original = rich();
    CoopLootLedger restored;
    std::string error;
    ASSERT_TRUE(parse(original.serialize(), restored, error)) << error;
    EXPECT_EQ(original, restored);
    EXPECT_TRUE(restored.isTaken("tar_m02aa", "chest@3,-4,0", "guest-two", 2));
    EXPECT_FALSE(restored.isTaken("tar_m02aa", "chest@3,-4,0", "guest-two", 0));
    ASSERT_NE(nullptr, restored.find("tar_m03aa", "chest@3,-4,0"));
    EXPECT_EQ(3, restored.find("tar_m03aa", "chest@3,-4,0")->baseline[0].stack);
}

TEST(CoopLootLedgerSave, equal_ledgers_write_equal_bytes) {
    EXPECT_EQ(rich().serialize(), rich().serialize());
    CoopLootLedger reparsed;
    std::string error;
    ASSERT_TRUE(parse(rich().serialize(), reparsed, error));
    EXPECT_EQ(rich().serialize(), reparsed.serialize());
}

TEST(CoopLootLedgerSave, an_empty_ledger_round_trips) {
    CoopLootLedger empty, restored = rich();
    std::string error;
    ASSERT_TRUE(parse(empty.serialize(), restored, error)) << error;
    EXPECT_EQ(0, restored.size());
}

TEST(CoopLootLedgerSave, a_failed_parse_leaves_the_ledger_untouched) {
    CoopLootLedger ledger = rich();
    std::string error;
    EXPECT_FALSE(parse("{\"version\":1,\"records\":[{\"module\":", ledger, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(rich(), ledger);
}

TEST(CoopLootLedgerSave, rejects_wrong_versions_shapes_and_values) {
    const char *bad[] = {
        "",
        "null",
        "[]",
        "{}",
        R"({"version":2,"records":[]})",
        R"({"version":"1","records":[]})",
        R"({"version":1})",
        R"({"version":1,"records":{}})",
        R"({"version":1,"records":[1]})",
        R"({"version":1,"records":[{"module":1,"container":"c","baseline":[],"taken":{}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":{},"taken":{}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[],"taken":[]}]})",
        // an entry with a wrong type, a missing field, a stack out of range
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[{"resref":1,"tag":"","stack":1,"plot":false,"credits":false}],"taken":{}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[{"resref":"a","tag":"","stack":1,"plot":false}],"taken":{}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[{"resref":"a","tag":"","stack":70000,"plot":false,"credits":false}],"taken":{}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[{"resref":"a","tag":"","stack":-1,"plot":false,"credits":false}],"taken":{}}]})",
        // taking an item the container never had
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[],"taken":{"g":[0]}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[],"taken":{"g":[-1]}}]})",
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[],"taken":{"g":["x"]}}]})",
        // the same container twice
        R"({"version":1,"records":[{"module":"m","container":"c","baseline":[],"taken":{}},{"module":"m","container":"c","baseline":[],"taken":{}}]})",
    };
    for (const char *text : bad) {
        CoopLootLedger ledger = rich();
        std::string error;
        EXPECT_FALSE(parse(text, ledger, error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        EXPECT_EQ(rich(), ledger) << text;
    }
}

TEST(CoopLootLedgerSave, rejects_huge_inputs_without_building_them) {
    std::string error;
    CoopLootLedger ledger;

    // More guests than a session can have.
    std::string guests = R"({"version":1,"records":[{"module":"m","container":"c","baseline":[],"taken":{)";
    for (int i = 0; i < 40; ++i) {
        guests += (i ? "," : "") + std::string("\"g") + std::to_string(i) + "\":[]";
    }
    guests += "}}]}";
    EXPECT_FALSE(parse(guests, ledger, error));

    // An over-long name.
    std::string longName = R"({"version":1,"records":[{"module":")" + std::string(100000, 'x') +
                           R"(","container":"c","baseline":[],"taken":{}}]})";
    EXPECT_FALSE(parse(longName, ledger, error));

    // A file bigger than any real ledger, and nesting deeper than the format has.
    EXPECT_FALSE(parse(std::string(9u * 1024 * 1024, ' '), ledger, error));
    EXPECT_FALSE(parse(std::string(100000, '[') + std::string(100000, ']'), ledger, error));
    EXPECT_EQ(0, ledger.size());
}

TEST(CoopLootLedgerSave, every_truncation_and_single_byte_damage_fails_cleanly_or_parses) {
    const std::string good = rich().serialize();
    for (size_t length = 0; length < good.size(); ++length) {
        CoopLootLedger ledger;
        std::string error;
        bool ok = parse(good.substr(0, length), ledger, error);
        if (ok) {
            // A prefix can only be whole if nothing was cut off.
            ADD_FAILURE() << "a " << length << "-byte prefix parsed";
        }
    }
    for (size_t at = 0; at < good.size(); ++at) {
        for (char byte : {'\0', '"', '}', '9', '-', 'x'}) {
            std::string damaged = good;
            damaged[at] = byte;
            CoopLootLedger ledger;
            std::string error;
            bool ok = parse(damaged, ledger, error); // must not crash or hang either way
            if (!ok) {
                EXPECT_FALSE(error.empty());
                EXPECT_EQ(0, ledger.size());
            }
        }
    }
}
