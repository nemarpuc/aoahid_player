// SPDX-License-Identifier: MIT
// Covers how a recording writes touch coordinates (raw, virtual, normalized)
// and the --coords spellings; the adb side of the recorder needs a phone.
#include "doctest.h"

#include "aoahid_player/event_script.hpp"
#include "aoahid_player/recorder.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <vector>

namespace {

std::vector<aoap::EventRecord> sample_rows() {
    return {
        {aoap::TouchEvent{0, true, 540, 1200}, false, 16'000'000},
        {aoap::KeyEvent{0x04, true}, false, 8'000'000},
    };
}

} // namespace

TEST_CASE("parse_coord_mode reads the command-line spellings") {
    aoap::CoordMode mode = aoap::CoordMode::normalized;
    int32_t size = 0;
    std::string error;

    REQUIRE(aoap::parse_coord_mode("raw", mode, size, error));
    CHECK(mode == aoap::CoordMode::raw);
    REQUIRE(aoap::parse_coord_mode("virtual", mode, size, error));
    CHECK(mode == aoap::CoordMode::virtual_space);
    CHECK(size == 32768);
    REQUIRE(aoap::parse_coord_mode("virtual=4096", mode, size, error));
    CHECK(size == 4096);
    REQUIRE(aoap::parse_coord_mode("normalized", mode, size, error));
    CHECK(mode == aoap::CoordMode::normalized);

    CHECK_FALSE(aoap::parse_coord_mode("pixel", mode, size, error));
    CHECK(error.find("pixel") != std::string::npos);
    CHECK_FALSE(aoap::parse_coord_mode("virtual=1", mode, size, error));
    CHECK_FALSE(aoap::parse_coord_mode("virtual=70000", mode, size, error));
    CHECK_FALSE(aoap::parse_coord_mode("virtual=abc", mode, size, error));
    CHECK_FALSE(aoap::parse_coord_mode("", mode, size, error));
}

TEST_CASE("A recording writes coordinates in the chosen mode") {
    const std::vector<aoap::EventRecord> rows = sample_rows();
    std::string text;

    aoap::RecordFormat raw{aoap::CoordMode::raw, 1080, 2400, 32768};
    CHECK(aoap::record_header(raw) == "# screen 1080x2400\n");
    aoap::append_recorded_rows(text, rows, raw);
    CHECK(text == "t,0,1,540,1200,16.000\nk,0x04,1,8.000\n");

    text.clear();
    aoap::RecordFormat virtual_space{aoap::CoordMode::virtual_space, 1080, 2400, 32768};
    CHECK(aoap::record_header(virtual_space) == "# screen 32768x32768\n");
    aoap::append_recorded_rows(text, rows, virtual_space);
    CHECK(text == "t,0,1,16384,16384,16.000\nk,0x04,1,8.000\n");

    text.clear();
    aoap::RecordFormat normalized{aoap::CoordMode::normalized, 1080, 2400, 32768};
    CHECK(aoap::record_header(normalized) == "@format 2\n@coords normalized\n");
    aoap::append_recorded_rows(text, rows, normalized);
    CHECK(text == "t,0,1,0.500000,0.500000,16.000\nk,0x04,1,8.000\n");

    // The panel's range is unknown: nothing to convert from, so rows stay raw.
    text.clear();
    aoap::RecordFormat unknown{aoap::CoordMode::raw, 0, 0, 32768};
    CHECK(aoap::record_header(unknown).empty());
    aoap::append_recorded_rows(text, rows, unknown);
    CHECK(text == "t,0,1,540,1200,16.000\nk,0x04,1,8.000\n");
}

TEST_CASE("Recordings in every mode play back at the same place") {
    const std::vector<aoap::EventRecord> rows = sample_rows();
    const aoap::CoordMode modes[] = {aoap::CoordMode::raw, aoap::CoordMode::virtual_space,
                                     aoap::CoordMode::normalized};
    for (const aoap::CoordMode mode : modes) {
        const aoap::RecordFormat format{mode, 1080, 2400, 32768};
        std::string text = aoap::record_header(format);
        aoap::append_recorded_rows(text, rows, format);

        const std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("aoahid_player_test_record_" + std::to_string(::rand()) + ".csv");
        {
            std::ofstream out(path, std::ios::binary);
            out << text;
        }
        aoap::EventScript script;
        std::string error;
        REQUIRE(script.load(path.string(), error));
        std::filesystem::remove(path);

        // Onto a 1080x2400 surface, and onto a different one.
        aoap::EventScript same;
        if (!aoap::scale_script(script, 1080, 2400, 0, 0, same))
            same = script;
        CHECK(std::get<aoap::TouchEvent>(same.rows[0].payload).x == 540);
        CHECK(std::get<aoap::TouchEvent>(same.rows[0].payload).y == 1200);

        aoap::EventScript other;
        REQUIRE(aoap::scale_script(script, 4096, 4096, 0, 0, other));
        CHECK(std::get<aoap::TouchEvent>(other.rows[0].payload).x == 2048);
        CHECK(std::get<aoap::TouchEvent>(other.rows[0].payload).y == 2048);
    }
}

TEST_CASE("A position slightly past the panel range is kept inside the file's range") {
    const std::vector<aoap::EventRecord> rows = {
        {aoap::TouchEvent{0, true, 1085, -3}, false, 1'000'000},
    };
    aoap::RecordFormat normalized{aoap::CoordMode::normalized, 1080, 2400, 32768};
    std::string text = aoap::record_header(normalized);
    aoap::append_recorded_rows(text, rows, normalized);
    CHECK(text.find("t,0,1,1.000000,0.000000,1.000") != std::string::npos);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("aoahid_player_test_clamp_" + std::to_string(::rand()) + ".csv");
    {
        std::ofstream out(path, std::ios::binary);
        out << text;
    }
    aoap::EventScript script;
    std::string error;
    CHECK(script.load(path.string(), error)); // a recording must always load
    std::filesystem::remove(path);
}
