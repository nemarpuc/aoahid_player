// SPDX-License-Identifier: MIT
// Covers spec_detail::unsigned_bits() and axis_is_unipolar(), the small pure
// functions inside spec_builder.hpp that decide HID logical-range bit
// widths. The rest of spec_builder.hpp calls into libaoahid itself (spec
// creation), which needs a real device/context to exercise meaningfully and
// is out of scope for a unit test.
#include <ostream>

#include "doctest.h"

#include "aoahid_player/spec_builder.hpp"

using aoap::spec_detail::axis_is_unipolar;
using aoap::spec_detail::unsigned_bits;

TEST_CASE("unsigned_bits picks the smallest width that represents the maximum") {
    CHECK(unsigned_bits(0) == 1);
    CHECK(unsigned_bits(1) == 1);
    CHECK(unsigned_bits(2) == 2);
    CHECK(unsigned_bits(15) == 4);   // e.g. 15 simultaneous touch contacts
    CHECK(unsigned_bits(255) == 8);
    CHECK(unsigned_bits(1079) == 11); // 1080-wide touch surface, width - 1
    CHECK(unsigned_bits(1919) == 11); // 1920-tall touch surface, height - 1
    CHECK(unsigned_bits(4095) == 12); // 12-bit pressure, the CLI default
}

TEST_CASE("unsigned_bits never returns a width the value doesn't fit in") {
    for (const int64_t maximum : {0, 1, 7, 8, 9, 1023, 1024, 1025, 65535, 65536}) {
        const uint32_t bits = unsigned_bits(maximum);
        REQUIRE(bits >= 1);
        REQUIRE(bits <= 32);
        // (1 << bits) - 1 must be able to represent `maximum`.
        CHECK((int64_t{1} << bits) - 1 >= maximum);
    }
}

TEST_CASE("axis_is_unipolar matches only the 0..max simulation axes") {
    CHECK(axis_is_unipolar(AOAHID_AXIS_SIMULATION_ACCELERATOR));
    CHECK(axis_is_unipolar(AOAHID_AXIS_SIMULATION_BRAKE));
    CHECK(axis_is_unipolar(AOAHID_AXIS_SIMULATION_THROTTLE));

    CHECK_FALSE(axis_is_unipolar(AOAHID_AXIS_X));
    CHECK_FALSE(axis_is_unipolar(AOAHID_AXIS_SIMULATION_STEERING)); // signed, not unipolar
    CHECK_FALSE(axis_is_unipolar(AOAHID_AXIS_SIMULATION_RUDDER));
}

TEST_CASE("find_axis resolves the same entry by name and by role") {
    const auto* by_name = aoap::spec_detail::find_axis(std::string_view{"steering"});
    const auto* by_role = aoap::spec_detail::find_axis(AOAHID_AXIS_SIMULATION_STEERING);
    REQUIRE(by_name != nullptr);
    REQUIRE(by_role != nullptr);
    CHECK(by_name == by_role);
    CHECK(by_name->linux_code == std::string_view{"ABS_WHEEL"});
}

TEST_CASE("find_axis returns nullptr for an unknown axis name") {
    CHECK(aoap::spec_detail::find_axis(std::string_view{"not-a-real-axis"}) == nullptr);
}

namespace {

aoap::ProfileSetup full_setup() {
    aoap::ProfileSetup setup;
    setup.touch = {true, 1080, 2400, 10};
    setup.mouse = {true, 5};
    setup.key = {true, 0x04, 0x65};
    setup.gamepad.enabled = true;
    setup.gamepad.buttons = 16;
    setup.gamepad.axis_bits = 16;
    setup.gamepad.dpad = AOAHID_DPAD_HAT;
    setup.gamepad.axes = {AOAHID_AXIS_X, AOAHID_AXIS_Y};
    setup.pen.enabled = true;
    setup.digitizer.enabled = true;
    setup.toggle.enabled = true;
    aoap::resolve_pen_surface(setup);
    return setup;
}

} // namespace

TEST_CASE("build_spec makes a Spec for every profile of a valid setup") {
    const aoap::ProfileSetup setup = full_setup();
    for (size_t index = 0; index < aoap::profile_count; ++index) {
        const auto profile = static_cast<aoap::Profile>(index);
        aoahid_spec* spec = nullptr;
        std::string error;
        CAPTURE(aoap::profile_name(profile));
        REQUIRE(aoap::build_spec(profile, setup, spec, error));
        CHECK(spec != nullptr);
        CHECK(error.empty());
        aoahid_spec_release(spec);
    }
}

TEST_CASE("the pen and the digitizer build Specs of their own profiles") {
    const aoap::ProfileSetup setup = full_setup();
    const struct {
        aoap::Profile profile;
        aoahid_profile_kind kind;
        aoahid_android_status status;
    } cases[] = {
        {aoap::Profile::pen, AOAHID_PROFILE_PEN, AOAHID_ANDROID_PORTABLE_CANDIDATE},
        {aoap::Profile::digitizer, AOAHID_PROFILE_DIGITIZER, AOAHID_ANDROID_CONDITIONAL},
    };
    for (const auto& entry : cases) {
        aoahid_spec* spec = nullptr;
        std::string error;
        CAPTURE(aoap::profile_name(entry.profile));
        REQUIRE(aoap::build_spec(entry.profile, setup, spec, error));
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<uint32_t>(sizeof manifest);
        REQUIRE(aoahid_spec_manifest(spec, &manifest) == AOAHID_OK);
        CHECK(manifest.profile_kind == entry.kind);
        CHECK(manifest.android_status == entry.status);
        aoahid_spec_release(spec);
    }
}

TEST_CASE("validate_profile checks one profile and ignores the others") {
    aoap::ProfileSetup setup = full_setup();
    setup.touch.width = 0;
    std::string error;
    CHECK_FALSE(aoap::validate_profile(setup, aoap::Profile::touch, error));
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK(aoap::validate_profile(setup, aoap::Profile::mouse, error));
    CHECK_FALSE(aoap::validate_setup(setup, error));

    setup.touch.width = 1080;
    setup.touch.max_contacts = 17;
    CHECK_FALSE(aoap::validate_profile(setup, aoap::Profile::touch, error));
    setup.touch.max_contacts = 16;
    setup.key.usage_maximum = 0xE0;
    CHECK_FALSE(aoap::validate_profile(setup, aoap::Profile::key, error));
}

TEST_CASE("a setup with no profile is valid: the device stays connected with no HID") {
    const aoap::ProfileSetup none;
    std::string error;
    CHECK(aoap::validate_setup(none, error));
    CHECK(aoap::enabled_profiles(none) == 0U);
}

TEST_CASE("same_profile compares one profile's settings, the pen surface included") {
    aoap::ProfileSetup a = full_setup();
    aoap::ProfileSetup b = a;
    CHECK(a == b);
    b.touch.width = 1440;
    aoap::resolve_pen_surface(b);
    CHECK_FALSE(aoap::same_profile(a, b, aoap::Profile::touch));
    CHECK_FALSE(aoap::same_profile(a, b, aoap::Profile::pen)); // the pen shares the touch surface
    CHECK_FALSE(aoap::same_profile(a, b, aoap::Profile::digitizer)); // and so does the digitizer
    CHECK(aoap::same_profile(a, b, aoap::Profile::mouse));
    aoap::set_profile_enabled(b, aoap::Profile::mouse, false);
    CHECK_FALSE(aoap::profile_enabled(b, aoap::Profile::mouse));
    CHECK_FALSE(aoap::same_profile(a, b, aoap::Profile::mouse));
}
