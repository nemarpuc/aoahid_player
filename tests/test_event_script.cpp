// SPDX-License-Identifier: MIT
// Covers EventScript::load() (CSV row parsing, wait_ms -> wait_ns
// conversion, comment/BOM handling) and batch_key().
#include "doctest.h"

#include "aoahid_player/event_script.hpp"

#include <ostream>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// Writes `content` to a fresh temp file and returns its path. Each call uses
// a distinct name so parallel/-repeated test runs can't collide.
std::string write_temp_csv(const std::string& name, const std::string& content) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("aoahid_player_test_" + name + "_" + std::to_string(::rand()) + ".csv");
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path.string();
}

} // namespace

TEST_CASE("EventScript::load parses one row of every profile") {
    const std::string path = write_temp_csv("all_profiles",
        "t,1,1,100,200,16.5\n"
        "m,10,-5,5\n"
        "b,1,1,5\n"
        "k,0x04,1,5\n"
        "g,1,1,5\n"
        "a,0,32767,5\n"
        "h,1,0,0,1,5\n"
        "p,1,1,50,60,2000,5\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    CHECK(error.empty());
    CHECK(script.rows.size() == 8);

    const auto& touch = std::get<aoap::TouchEvent>(script.rows[0].payload);
    CHECK(touch.finger_id == 1);
    CHECK(touch.state == true);
    CHECK(touch.x == 100);
    CHECK(touch.y == 200);
    // 16.5ms -> 16,500,000ns; the multiply happens once at parse time.
    CHECK(script.rows[0].wait_ns == 16'500'000);

    const auto& key = std::get<aoap::KeyEvent>(script.rows[3].payload);
    CHECK(key.usage == 0x04); // hex column accepted via the 0x.. prefix
    CHECK(key.down == true);

    const auto& pen = std::get<aoap::PenSample>(script.rows[7].payload);
    CHECK(pen.in_range == true);
    CHECK(pen.tip == true);
    CHECK(pen.pressure == 2000);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load skips comments, blank lines, and a UTF-8 BOM") {
    const std::string path = write_temp_csv(
        "comments",
        "\xEF\xBB\xBF# leading comment on the BOM line\n"
        "\n"
        "   \n"
        "t,1,1,0,0,5 # trailing comment\n"
        "# whole-line comment\n"
        "t,2,1,0,0,5\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    CHECK(script.size() == 2);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load rejects a malformed row with a file:line message") {
    const std::string path = write_temp_csv("broken", "t,0,1,500\n"); // missing a column

    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load(path, error));
    CHECK(error.find(":1:") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load rejects pen tip=1 without in_range=1") {
    const std::string path = write_temp_csv("pen_invariant", "p,0,1,50,60,2000,5\n");

    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load(path, error));
    CHECK(error.find("in_range") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load rejects an unknown row prefix") {
    const std::string path = write_temp_csv("bad_prefix", "z,1,2,3\n");

    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load(path, error));

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load rejects a file with no event rows") {
    const std::string path = write_temp_csv("empty", "# only a comment\n\n");

    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load(path, error));
    CHECK(error.find("no event rows") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load reports a missing file") {
    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load("/no/such/path/really.csv", error));
    CHECK(error.find("cannot open") != std::string::npos);
}

TEST_CASE("batch_key distinguishes controls and merges mouse moves") {
    using aoap::batch_key;

    CHECK(batch_key(aoap::TouchEvent{0, true, 0, 0}) != batch_key(aoap::TouchEvent{1, true, 0, 0}));
    CHECK(batch_key(aoap::TouchEvent{3, true, 10, 20}) ==
          batch_key(aoap::TouchEvent{3, false, 99, 99})); // key ignores state/x/y, only finger_id
    CHECK(batch_key(aoap::MouseMove{1, 1}) == batch_key(aoap::MouseMove{-5, 8}));
    CHECK(batch_key(aoap::KeyEvent{0x04, true}) != batch_key(aoap::MouseButton{1, true}));
}

TEST_CASE("scale_script maps touch and pen rows onto the connected surfaces") {
    aoap::EventScript script;
    script.screen_width = 4096;
    script.screen_height = 4096;
    script.rows.push_back({aoap::TouchEvent{0, true, 0, 0}, 0});
    script.rows.push_back({aoap::TouchEvent{1, true, 2048, 4095}, 1'000'000});
    script.rows.push_back({aoap::PenSample{true, true, 4095, 1024, 100}, 0});
    script.rows.push_back({aoap::KeyEvent{0x04, true}, 0});

    aoap::EventScript scaled;
    REQUIRE(aoap::scale_script(script, 1080, 2400, 32768, 32768, scaled));
    const auto& first = std::get<aoap::TouchEvent>(scaled.rows[0].payload);
    CHECK(first.x == 0);
    CHECK(first.y == 0);
    const auto& moved = std::get<aoap::TouchEvent>(scaled.rows[1].payload);
    CHECK(moved.x == 540);  // the middle stays the middle
    CHECK(moved.y == 2399); // the last coordinate stays inside the surface
    const auto& pen = std::get<aoap::PenSample>(scaled.rows[2].payload);
    CHECK(pen.x == 32767);
    CHECK(pen.y == 8194);
    CHECK(std::get<aoap::KeyEvent>(scaled.rows[3].payload).usage == 0x04);
    CHECK(scaled.rows[1].wait_ns == 1'000'000); // timing untouched
    CHECK(scaled.screen_width == 1080);

    aoap::EventScript untouched;
    CHECK_FALSE(aoap::scale_script(script, 4096, 4096, 0, 0, untouched)); // same size
    aoap::EventScript undeclared;
    undeclared.rows = script.rows;
    CHECK_FALSE(aoap::scale_script(undeclared, 1080, 2400, 0, 0, untouched)); // no space
}

TEST_CASE("EventScript::load reports every bad row, not only the first") {
    const std::string path = write_temp_csv("many_errors",
        "t,0,1,500\n"        // line 1: missing a column
        "z,1\n"              // line 2: unknown prefix
        "k,0x04,1,5\n"       // line 3: fine
        "t,0,1,a,2,3\n");    // line 4: not a number

    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load(path, error));
    REQUIRE(script.errors.size() == 3);
    CHECK(script.errors[0].find(":1:") != std::string::npos);
    CHECK(script.errors[1].find(":2:") != std::string::npos);
    CHECK(script.errors[2].find(":4:") != std::string::npos);
    CHECK(error.find(":1:") != std::string::npos);
    CHECK(error.find("and 2 more") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("Reading stops after too many errors") {
    std::string text;
    for (int index = 0; index < 80; ++index)
        text += "z,1\n";
    aoap::EventScript script;
    std::string error;
    CHECK_FALSE(script.load(write_temp_csv("flood", text), error));
    CHECK(script.errors.size() == 51); // 50 rows, then a note that reading stopped
    CHECK(script.errors.back().find("too many") != std::string::npos);
}

TEST_CASE("Key rows accept names as well as usage numbers") {
    const std::string path = write_temp_csv("key_names",
        "k,A,1,1\n"
        "k,z,1,1\n"
        "k,Enter,1,1\n"
        "k,LShift,1,1\n"
        "k,Shift,1,1\n"
        "k,F5,1,1\n"
        "k,Digit1,1,1\n"
        "k,0x04,1,1\n"
        "k,1,1,1\n"); // a number stays a usage number

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 9);
    const auto usage = [&](const size_t index) {
        return std::get<aoap::KeyEvent>(script.rows[index].payload).usage;
    };
    CHECK(usage(0) == 0x04);
    CHECK(usage(1) == 0x1D);
    CHECK(usage(2) == 0x28);
    CHECK(usage(3) == 0xE1);
    CHECK(usage(4) == 0xE1);
    CHECK(usage(5) == 0x3E);
    CHECK(usage(6) == 0x1E);
    CHECK(usage(7) == 0x04);
    CHECK(usage(8) == 0x01);
    std::filesystem::remove(path);

    aoap::EventScript bad;
    CHECK_FALSE(bad.load(write_temp_csv("key_unknown", "k,Banana,1,1\n"), error));
    CHECK(error.find("Banana") != std::string::npos);
}

TEST_CASE("@coords normalized reads touch and pen positions as fractions") {
    const std::string path = write_temp_csv("normalized",
        "@format 3\n"
        "@coords normalized\n"
        "t,0,1,0.5,0.25,0\n"
        "p,1,1,1,0,100,0\n"
        "t,1,1,0,1,0\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    CHECK(script.coords_normalized);
    CHECK(script.screen_width == 65536);
    CHECK(script.screen_height == 65536);

    const auto& touch = std::get<aoap::TouchEvent>(script.rows[0].payload);
    CHECK(touch.x == 32768);
    CHECK(touch.y == 16384);
    const auto& pen = std::get<aoap::PenSample>(script.rows[1].payload);
    CHECK(pen.x == 65535); // 1.0 stays inside the space
    CHECK(pen.y == 0);
    CHECK(pen.pressure == 100); // only positions are fractions
    const auto& corner = std::get<aoap::TouchEvent>(script.rows[2].payload);
    CHECK(corner.x == 0);
    CHECK(corner.y == 65535);

    aoap::EventScript scaled;
    REQUIRE(aoap::scale_script(script, 1080, 2400, 0, 0, scaled));
    CHECK(std::get<aoap::TouchEvent>(scaled.rows[0].payload).x == 540);
    CHECK(std::get<aoap::TouchEvent>(scaled.rows[0].payload).y == 600);
    CHECK(std::get<aoap::TouchEvent>(scaled.rows[2].payload).y == 2399);

    std::filesystem::remove(path);
}

TEST_CASE("@coords mistakes are reported") {
    aoap::EventScript script;
    std::string error;

    CHECK_FALSE(script.load(write_temp_csv("nrm_high", "@coords normalized\nt,0,1,1.5,0,0\n"), error));
    CHECK(error.find("fraction") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("nrm_neg", "@coords normalized\nt,0,1,-0.1,0,0\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("nrm_text", "@coords normalized\nt,0,1,abc,0,0\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("nrm_screen1", "@screen 100x100\n@coords normalized\nt,0,1,0,0,0\n"),
                            error));
    CHECK_FALSE(script.load(write_temp_csv("nrm_screen2", "@coords normalized\n@screen 100x100\nt,0,1,0,0,0\n"),
                            error));
    CHECK_FALSE(script.load(write_temp_csv("nrm_bogus", "@coords bogus\nt,0,1,0,0,0\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("nrm_late", "t,0,1,5,5,0\n@coords normalized\n"), error));
    CHECK(error.find("before the first row") != std::string::npos);

    // Integers are still integers unless the file says otherwise.
    REQUIRE(script.load(write_temp_csv("nrm_integer", "@coords integer\nt,0,1,5,5,0\n"), error));
    CHECK_FALSE(script.coords_normalized);
    CHECK(std::get<aoap::TouchEvent>(script.rows[0].payload).x == 5);
}

TEST_CASE("EventScript::load parses media key rows by name or usage") {
    const std::string path = write_temp_csv("media",
        "c,VolumeUp,1,5\n"
        "c,volumeup,0,5\n"
        "c,0xcd,1,0\n"
        "c,PlayPause,0,5\n"
        "c,Prev,1,5\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 5);
    const auto& up = std::get<aoap::MediaKey>(script.rows[0].payload);
    CHECK(up.usage == 0x00E9);
    CHECK(up.down == true);
    CHECK(std::get<aoap::MediaKey>(script.rows[1].payload).down == false);
    CHECK(std::get<aoap::MediaKey>(script.rows[2].payload).usage == 0x00CD);
    CHECK(std::get<aoap::MediaKey>(script.rows[4].payload).usage == 0x00B6);
    // One Consumer field: every media row conflicts with every other.
    CHECK(aoap::batch_key(script.rows[0].payload) == aoap::batch_key(script.rows[2].payload));
    CHECK(aoap::batch_key(script.rows[0].payload) != 0U);
    CHECK(script.required_profiles() == (uint32_t{1} << 5)); // Profile::toggle

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load parses the brightness keys by name or usage") {
    const std::string path = write_temp_csv("media_brightness",
        "c,BrightnessUp,1,5\n"
        "c,brightnessup,0,5\n"
        "c,BrightnessDown,1,5\n"
        "c,0x70,0,5\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 4);
    CHECK(std::get<aoap::MediaKey>(script.rows[0].payload).usage == 0x006F);
    CHECK(std::get<aoap::MediaKey>(script.rows[0].payload).down == true);
    CHECK(std::get<aoap::MediaKey>(script.rows[1].payload).usage == 0x006F);
    CHECK(std::get<aoap::MediaKey>(script.rows[2].payload).usage == 0x0070);
    CHECK(std::get<aoap::MediaKey>(script.rows[3].payload).usage == 0x0070);
    CHECK(std::get<aoap::MediaKey>(script.rows[3].payload).down == false);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load rejects media keys the toggle profile does not declare") {
    for (const char* row : {"c,Louder,1,5\n", "c,0x00E8,1,5\n", "c,VolumeUp,2,5\n",
                            "c,VolumeUp\n", "c,FastForward,1,5\n", "c,0x00B3,1,5\n",
                            "c,Rewind,1,5\n", "c,0x00B4,1,5\n"}) {
        const std::string path = write_temp_csv("media_bad", row);
        aoap::EventScript script;
        std::string error;
        CHECK_FALSE(script.load(path, error));
        CHECK_FALSE(error.empty());
        std::filesystem::remove(path);
    }
}

TEST_CASE("Rows outside a block form one segment that runs every lap") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("seg_plain", "k,A,1,10\nk,A,0,20\n"), error));
    REQUIRE(script.segments.size() == 1);
    const aoap::Segment& all = script.segments[0];
    CHECK(all.begin == 0);
    CHECK(all.end == 2);
    CHECK(all.lead_ns == 0);
    CHECK(all.duration == 30'000'000);
    CHECK(all.when.first == 1);
    CHECK(all.when.last == UINT64_MAX);
    CHECK(all.when.every == 1);
    CHECK_FALSE(all.when.negate);
    CHECK(script.lap_blocks == 0);
}

TEST_CASE("@lap and @every blocks become segments with their selector") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("seg_blocks",
                                       "k,A,1,1\n"
                                       "@lap 1\n"
                                       "k,B,1,2\n"
                                       "@end\n"
                                       "@lap 3..5 keep-time\n"
                                       "k,C,1,3\n"
                                       "@end\n"
                                       "@lap 7..\n"
                                       "k,D,1,4\n"
                                       "@end\n"
                                       "@every 10\n"
                                       "k,E,1,5\n"
                                       "@else\n"
                                       "k,F,1,6\n"
                                       "@end\n"
                                       "@every 4 from 2 to 14\n"
                                       "k,G,1,7\n"
                                       "@end\n"
                                       "k,A,0,8\n"),
                        error));
    REQUIRE(script.rows.size() == 8);
    REQUIRE(script.segments.size() == 8);
    CHECK(script.lap_blocks == 5);

    const auto same = [&](const size_t index, const uint64_t first, const uint64_t last,
                          const uint64_t every, const bool negate, const bool keep) {
        const aoap::LapSelector& when = script.segments[index].when;
        return when.first == first && when.last == last && when.every == every &&
               when.negate == negate && when.keep_time == keep;
    };
    CHECK(same(0, 1, UINT64_MAX, 1, false, false));
    CHECK(same(1, 1, 1, 1, false, false));
    CHECK(same(2, 3, 5, 1, false, true));
    CHECK(same(3, 7, UINT64_MAX, 1, false, false));
    CHECK(same(4, 10, UINT64_MAX, 10, false, false));
    CHECK(same(5, 10, UINT64_MAX, 10, true, false));
    CHECK(same(6, 2, 14, 4, false, false));
    CHECK(same(7, 1, UINT64_MAX, 1, false, false));
    for (uint32_t index = 0; index < 8; ++index) {
        CHECK(script.segments[index].begin == index);
        CHECK(script.segments[index].end == index + 1);
    }
}

TEST_CASE("A wait row adds to the row before it, or leads its segment") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("wait_rows",
                                       "w,5\n"       // leads the first segment
                                       "k,A,1\n"     // no wait_ms: 0
                                       "w,10\n"      // onto k,A,1
                                       "w,2.5\n"     // and again
                                       "@every 2\n"
                                       "w,7\n"       // leads the block
                                       "k,B,1,1\n"
                                       "@else\n"
                                       "w,30\n"      // a part with no rows at all
                                       "@end\n"
                                       "k,A,0\n"),   // last row, no wait
                        error));
    REQUIRE(script.rows.size() == 3);
    CHECK(script.rows[0].wait_ns == 12'500'000);
    CHECK(script.rows[2].wait_ns == 0);
    REQUIRE(script.segments.size() == 4);
    CHECK(script.segments[0].lead_ns == 5'000'000);
    CHECK(script.segments[0].duration == 17'500'000);
    CHECK(script.segments[1].lead_ns == 7'000'000);
    CHECK(script.segments[1].duration == 8'000'000);
    CHECK(script.segments[2].begin == script.segments[2].end);
    CHECK(script.segments[2].lead_ns == 30'000'000);
    CHECK(script.segments[2].duration == 30'000'000);
    CHECK(script.segments[2].when.negate);
    CHECK(script.segments[3].duration == 0);
}

TEST_CASE("Every row type takes its wait_ms or leaves it out") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("no_wait",
                                       "t,0,1,10,20\nm,1,2\nb,1,1\nk,A,1\ng,1,1\na,0,5\n"
                                       "h,1,0,0,0\np,1,1,5,6,7\nc,VolumeUp,1\n"),
                        error));
    REQUIRE(script.rows.size() == 9);
    for (const aoap::EventRecord& row : script.rows)
        CHECK(row.wait_ns == 0);
    CHECK(std::get<aoap::TouchEvent>(script.rows[0].payload).y == 20);

    CHECK_FALSE(script.load(write_temp_csv("short_row", "t,0,1,10\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("long_row", "k,A,1,5,5\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("wait_cols", "k,A,1,1\nw,5,5\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("wait_bad", "k,A,1,1\nw,-1\n"), error));
    // Waits alone are not a script.
    CHECK_FALSE(script.load(write_temp_csv("wait_only", "w,100\n"), error));
    CHECK(error.find("no event rows") != std::string::npos);
}

TEST_CASE("Old-format files are refused with what to write instead") {
    aoap::EventScript script;
    std::string error;

    CHECK_FALSE(script.load(write_temp_csv("old_upper", "K,A,1,8\n"), error));
    CHECK(error.find(":1:") != std::string::npos);
    CHECK(error.find("@lap 1") != std::string::npos);

    CHECK_FALSE(script.load(write_temp_csv("old_once", "@once\nk,A,1,8\n@end\n"), error));
    CHECK(error.find("@lap 1") != std::string::npos);

    CHECK_FALSE(script.load(write_temp_csv("old_screen", "# screen 1080x2400\nt,0,1,5,5,0\n"),
                            error));
    CHECK(error.find("@screen") != std::string::npos);
    // An ordinary comment that merely starts with the word is still a comment.
    REQUIRE(script.load(write_temp_csv("screensaver", "# screensaver 2x3\nt,0,1,5,5,0\n"), error));
    CHECK(script.screen_width == 0);

    for (const char* version : {"1", "2", "4"}) {
        CHECK_FALSE(script.load(write_temp_csv("old_format", std::string("@format ") + version +
                                                                 "\nt,0,1,5,5,0\n"),
                                error));
        CHECK(error.find("@format 3") != std::string::npos);
    }
}

TEST_CASE("@format 3 and @screen directives") {
    aoap::EventScript script;
    std::string error;

    REQUIRE(script.load(write_temp_csv("dir_ok", "@format 3\n@screen 1080x2400\nt,0,1,5,5,0\n"),
                        error));
    CHECK(script.screen_width == 1080);
    CHECK(script.screen_height == 2400);

    CHECK_FALSE(script.load(write_temp_csv("dir_late", "t,0,1,5,5,0\n@screen 1080x2400\n"), error));
    CHECK(error.find("before the first row") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("dir_bad_size", "@screen 10x\nt,0,1,5,5,0\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("dir_twice", "@screen 1x1\n@screen 2x2\nt,0,1,5,5,0\n"),
                            error));
    // A wait row counts as the first row too.
    CHECK_FALSE(script.load(write_temp_csv("dir_after_wait", "w,5\n@screen 1x1\nt,0,1,5,5,0\n"),
                            error));
}

TEST_CASE("Block mistakes are reported") {
    aoap::EventScript script;
    std::string error;
    const auto refused = [&](const char* name, const char* text, const char* said) {
        CAPTURE(name);
        CHECK_FALSE(script.load(write_temp_csv(name, text), error));
        CHECK(error.find(said) != std::string::npos);
    };
    refused("blk_nested", "@lap 1\n@every 2\nt,0,1,1,1,0\n@end\n", "nested");
    refused("blk_stray_end", "t,0,1,1,1,0\n@end\n", "@end");
    refused("blk_stray_else", "t,0,1,1,1,0\n@else\n", "@else");
    refused("blk_two_else", "@lap 1\nk,A,1\n@else\nk,B,1\n@else\nk,C,1\n@end\n", "@else");
    refused("blk_else_keep", "@lap 1 keep-time\nk,A,1\n@else\nk,B,1\n@end\n", "keep-time");
    refused("blk_else_arg", "@lap 1\nk,A,1\n@else 2\nk,B,1\n@end\n", "@else");
    refused("blk_open", "@lap 1\nt,0,1,1,1,0\n", "not closed");
    refused("blk_unknown", "@wat\nt,0,1,1,1,0\n", "unknown directive");
    refused("lap_none", "@lap\nk,A,1\n@end\n", "@lap");
    refused("lap_zero", "@lap 0\nk,A,1\n@end\n", "@lap");
    refused("lap_back", "@lap 5..3\nk,A,1\n@end\n", "@lap");
    refused("lap_option", "@lap 1 fast\nk,A,1\n@end\n", "@lap");
    refused("lap_text", "@lap x..\nk,A,1\n@end\n", "@lap");
    refused("every_none", "@every\nk,A,1\n@end\n", "@every");
    refused("every_zero", "@every 0\nk,A,1\n@end\n", "@every");
    refused("every_from", "@every 3 from\nk,A,1\n@end\n", "@every");
    refused("every_order", "@every 3 to 9 from 2\nk,A,1\n@end\n", "@every");
    refused("every_back", "@every 3 to 2\nk,A,1\n@end\n", "@every");
    refused("every_extra", "@every 3 from 1 to 9 keep-time now\nk,A,1\n@end\n", "@every");
}

TEST_CASE("Directive words and options ignore case") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("case_dir",
                                       "@FORMAT 3\n@Coords Normalized\n@Every 2 FROM 1 KEEP-TIME\n"
                                       "k,a,1,5\n@END\nt,0,1,0.5,0.5,0\n"),
                        error));
    CHECK(script.coords_normalized);
    REQUIRE(script.segments.size() == 2);
    CHECK(script.segments[0].when.every == 2);
    CHECK(script.segments[0].when.first == 1);
    CHECK(script.segments[0].when.keep_time);
}

TEST_CASE("The example script that ships with the program loads") {
    aoap::EventScript script;
    std::string error;
    REQUIRE_MESSAGE(script.load(AOAHID_PLAYER_CSV_DIR "/example.csv", error), error);
    CHECK(script.rows.size() == 19);
    CHECK(script.lap_blocks == 2);
}
