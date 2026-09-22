// SPDX-License-Identifier: MIT
// Covers EventScript::load() (CSV row parsing, wait_ms -> wait_ns
// conversion, once flags, comment/BOM handling) and batch_key().
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
    for (const aoap::EventRecord& row : script.rows)
        CHECK(row.once == false);

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

TEST_CASE("EventScript::load marks uppercase prefixes as once") {
    const std::string path =
        write_temp_csv("once_vs_loop", "T,1,1,0,0,0\nt,2,1,0,0,10\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 2);
    CHECK(script.rows[0].once == true);
    CHECK(script.rows[1].once == false);

    std::filesystem::remove(path);
}

TEST_CASE("EventScript::load keeps uppercase rows where they are written") {
    const std::string path = write_temp_csv(
        "once_in_place", "t,1,1,0,0,10\nT,2,1,0,0,20\nt,1,0,0,0,30\nK,0x04,1,40\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 4);
    CHECK(script.rows[0].once == false);
    CHECK(script.rows[1].once == true);
    CHECK(script.rows[2].once == false);
    CHECK(script.rows[3].once == true);
    CHECK(script.rows[1].wait_ns == 20'000'000);

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

TEST_CASE("Timeline gives the first lap every row and the repeat lap only the repeated rows") {
    aoap::EventScript script;
    script.rows.push_back({aoap::KeyEvent{0x04, true}, false, 10'000'000});
    script.rows.push_back({aoap::KeyEvent{0x05, true}, true, 20'000'000});
    script.rows.push_back({aoap::KeyEvent{0x06, true}, false, 30'000'000});
    script.rows.push_back({aoap::KeyEvent{0x07, true}, true, 40'000'000});

    const aoap::Timeline timeline = aoap::build_timeline(script);
    CHECK(timeline.first ==
          std::vector<int64_t>{0, 10'000'000, 30'000'000, 60'000'000, 100'000'000});
    CHECK(timeline.repeat == std::vector<int64_t>{0, 10'000'000, 40'000'000});
    CHECK(timeline.repeat_row == std::vector<uint32_t>{0, 2});
    CHECK(timeline.rows(aoap::Lap::first) == 4);
    CHECK(timeline.rows(aoap::Lap::repeat) == 2);
    CHECK(timeline.once_rows() == 2);
    CHECK(timeline.duration(aoap::Lap::first) == 100'000'000);
    CHECK(timeline.duration(aoap::Lap::repeat) == 40'000'000);
    CHECK(timeline.script_row(aoap::Lap::first, 3) == 3);
    CHECK(timeline.script_row(aoap::Lap::repeat, 1) == 2);

    REQUIRE(timeline.first_runs.size() == 4);
    CHECK(timeline.first_runs[1].once == true);
    CHECK(timeline.first_runs[1].start == 10'000'000);
    CHECK(timeline.first_runs[1].end == 30'000'000);
}

TEST_CASE("Timeline merges neighbouring rows of one kind into one run") {
    aoap::EventScript script;
    script.rows.push_back({aoap::KeyEvent{0x04, true}, true, 1'000'000});
    script.rows.push_back({aoap::KeyEvent{0x05, true}, true, 2'000'000});
    script.rows.push_back({aoap::KeyEvent{0x06, true}, false, 3'000'000});

    const aoap::Timeline timeline = aoap::build_timeline(script);
    REQUIRE(timeline.first_runs.size() == 2);
    CHECK(timeline.first_runs[0].once == true);
    CHECK(timeline.first_runs[0].start == 0);
    CHECK(timeline.first_runs[0].end == 3'000'000);
    CHECK(timeline.first_runs[1].once == false);
    CHECK(timeline.first_runs[1].end == 6'000'000);
}

TEST_CASE("Timeline without once rows has the same two laps") {
    aoap::EventScript script;
    script.rows.push_back({aoap::KeyEvent{0x04, true}, false, 5'000'000});
    script.rows.push_back({aoap::KeyEvent{0x04, false}, false, 7'000'000});

    const aoap::Timeline timeline = aoap::build_timeline(script);
    CHECK(timeline.once_rows() == 0);
    CHECK(timeline.first == timeline.repeat);
    REQUIRE(timeline.first_runs.size() == 1);
    CHECK(timeline.first_runs[0].once == false);
}

TEST_CASE("Timeline row_at finds seek rows") {
    aoap::EventScript script;
    script.rows.push_back({aoap::TouchEvent{0, true, 1, 1}, false, 0});
    script.rows.push_back({aoap::TouchEvent{1, true, 2, 2}, false, 10'000'000});
    script.rows.push_back({aoap::TouchEvent{0, false, 1, 1}, false, 5'000'000});

    const aoap::Timeline timeline = aoap::build_timeline(script);
    // Rows 0 and 1 share t=0 (a zero-wait batch); row 2 starts at 10 ms.
    CHECK(timeline.row_at(aoap::Lap::repeat, 0) == 0);
    CHECK(timeline.row_at(aoap::Lap::repeat, 1) == 2);
    CHECK(timeline.row_at(aoap::Lap::repeat, 10'000'000) == 2);
    CHECK(timeline.row_at(aoap::Lap::repeat, 10'000'001) == 3);
    CHECK(timeline.row_at(aoap::Lap::repeat, 99'000'000) == 3);
    CHECK(timeline.row_at(aoap::Lap::first, 10'000'001) == 3);
}

TEST_CASE("A '# screen WxH' comment names the coordinate space") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("screen_directive.csv",
                                       "# recorded by something\n"
                                       "#   Screen 4096X4096  \n"
                                       "# screen 1x1\n" // only the first one counts
                                       "t,0,1,2048,1024,0\n"),
                        error));
    CHECK(script.screen_width == 4096);
    CHECK(script.screen_height == 4096);

    aoap::EventScript plain;
    REQUIRE(plain.load(write_temp_csv("screen_none.csv", "# screensaver 2x3\nt,0,1,5,5,0\n"),
                       error));
    CHECK(plain.screen_width == 0);
    CHECK(plain.screen_height == 0);
}

TEST_CASE("scale_script maps touch and pen rows onto the connected surfaces") {
    aoap::EventScript script;
    script.screen_width = 4096;
    script.screen_height = 4096;
    script.rows.push_back({aoap::TouchEvent{0, true, 0, 0}, true, 0});
    script.rows.push_back({aoap::TouchEvent{1, true, 2048, 4095}, false, 1'000'000});
    script.rows.push_back({aoap::PenSample{true, true, 4095, 1024, 100}, false, 0});
    script.rows.push_back({aoap::KeyEvent{0x04, true}, false, 0});

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

TEST_CASE("@once ... @end marks the rows inside as once, whatever their case") {
    const std::string path = write_temp_csv("once_block",
        "t,0,1,10,10,5\n"
        "@once\n"
        "t,0,0,10,10,5\n" // lowercase inside the block is still once
        "k,A,1,5\n"
        "@end\n"
        "t,0,1,20,20,5\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 4);
    CHECK(script.rows[0].once == false);
    CHECK(script.rows[1].once == true);
    CHECK(script.rows[2].once == true);
    CHECK(script.rows[3].once == false);
    for (const aoap::EventRecord& row : script.rows)
        CHECK(row.keep_time == false);

    std::filesystem::remove(path);
}

TEST_CASE("@once keep-time keeps the waits of the skipped rows in later laps") {
    const std::string path = write_temp_csv("keep_time",
        "t,0,1,1,1,10\n"
        "@once keep-time\n"
        "k,A,1,20\n"
        "k,A,0,30\n"
        "@end\n"
        "t,0,0,1,1,40\n");

    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(path, error));
    REQUIRE(script.rows.size() == 4);
    CHECK(script.rows[1].keep_time == true);
    CHECK(script.rows[2].keep_time == true);
    CHECK(script.rows[3].keep_time == false);

    const aoap::Timeline timeline = aoap::build_timeline(script);
    CHECK(timeline.first == std::vector<int64_t>{0, 10'000'000, 30'000'000, 60'000'000, 100'000'000});
    // The once rows are skipped but their 50 ms still passes before row 3.
    CHECK(timeline.repeat == std::vector<int64_t>{0, 60'000'000, 100'000'000});
    CHECK(timeline.repeat_row == std::vector<uint32_t>{0, 3});

    std::filesystem::remove(path);
}

TEST_CASE("@format and @screen directives") {
    aoap::EventScript script;
    std::string error;

    REQUIRE(script.load(write_temp_csv("dir_ok", "@format 2\n@screen 1080x2400\nt,0,1,5,5,0\n"),
                        error));
    CHECK(script.screen_width == 1080);
    CHECK(script.screen_height == 2400);

    // @screen wins over the older comment form.
    REQUIRE(script.load(write_temp_csv("dir_over", "# screen 100x200\n@screen 1080x2400\nt,0,1,5,5,0\n"),
                        error));
    CHECK(script.screen_width == 1080);

    // Version 1 and 2 are the same language; anything newer is refused.
    REQUIRE(script.load(write_temp_csv("dir_v1", "@format 1\nt,0,1,5,5,0\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("dir_v3", "@format 3\nt,0,1,5,5,0\n"), error));
    CHECK(error.find("format") != std::string::npos);

    CHECK_FALSE(script.load(write_temp_csv("dir_late", "t,0,1,5,5,0\n@screen 1080x2400\n"), error));
    CHECK(error.find("before the first row") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("dir_bad_size", "@screen 10x\nt,0,1,5,5,0\n"), error));
    CHECK_FALSE(script.load(write_temp_csv("dir_twice", "@screen 1x1\n@screen 2x2\nt,0,1,5,5,0\n"),
                            error));
}

TEST_CASE("Block and directive mistakes are reported") {
    aoap::EventScript script;
    std::string error;

    CHECK_FALSE(script.load(write_temp_csv("blk_nested", "@once\n@once\nt,0,1,1,1,0\n@end\n"), error));
    CHECK(error.find("nested") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("blk_stray_end", "t,0,1,1,1,0\n@end\n"), error));
    CHECK(error.find("@end") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("blk_open", "@once\nt,0,1,1,1,0\n"), error));
    CHECK(error.find("not closed") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("blk_unknown", "@wat\nt,0,1,1,1,0\n"), error));
    CHECK(error.find("unknown directive") != std::string::npos);
    CHECK_FALSE(script.load(write_temp_csv("blk_option", "@once fast\nt,0,1,1,1,0\n@end\n"), error));
    CHECK(error.find("keep-time") != std::string::npos);
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
        "@format 2\n"
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

TEST_CASE("Directive words and options ignore case") {
    aoap::EventScript script;
    std::string error;
    REQUIRE(script.load(write_temp_csv("case_dir",
                                       "@FORMAT 2\n@Coords Normalized\n@Once KEEP-TIME\n"
                                       "k,a,1,5\n@END\nt,0,1,0.5,0.5,0\n"),
                        error));
    CHECK(script.coords_normalized);
    REQUIRE(script.rows.size() == 2);
    CHECK(script.rows[0].keep_time);
}
