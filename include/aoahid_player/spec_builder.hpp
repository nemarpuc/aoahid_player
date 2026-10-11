// SPDX-License-Identifier: MIT
#pragma once

#include <aoahid.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "context.hpp"
#include "device.hpp"
#include "key_names.hpp"

// Turns the chosen profile settings into the immutable aoahid_spec objects a
// connection uses. A Spec is built for one Node when it is opened and released
// right after; nothing here runs inside the playback loop.

namespace aoap {

struct TouchSetup {
    bool enabled{};
    int32_t width{};
    int32_t height{};
    uint32_t max_contacts{};

    bool operator==(const TouchSetup&) const = default;
};

struct MouseSetup {
    bool enabled{};
    uint32_t buttons{};

    bool operator==(const MouseSetup&) const = default;
};

struct KeySetup {
    bool enabled{};
    uint16_t usage_minimum{0x04};
    uint16_t usage_maximum{0x65};

    bool operator==(const KeySetup&) const = default;
};

struct GamepadSetup {
    bool enabled{};
    uint32_t buttons{};
    uint32_t axis_bits{};
    aoahid_dpad_representation dpad{AOAHID_DPAD_NONE};
    std::vector<aoahid_axis_role> axes;

    bool operator==(const GamepadSetup&) const = default;
};

struct PenSetup {
    bool enabled{};
    int32_t width{};
    int32_t height{};
    int32_t pressure_maximum{4095};

    bool operator==(const PenSetup&) const = default;
};

// The same stylus report shape as the pen, under the Digitizer Application
// Collection instead of Pen.
struct DigitizerSetup {
    bool enabled{};
    int32_t width{};
    int32_t height{};
    int32_t pressure_maximum{4095};

    bool operator==(const DigitizerSetup&) const = default;
};

struct ToggleSetup {
    bool enabled{};

    bool operator==(const ToggleSetup&) const = default;
};

struct ProfileSetup {
    TouchSetup touch;
    MouseSetup mouse;
    KeySetup key;
    GamepadSetup gamepad;
    PenSetup pen;
    DigitizerSetup digitizer;
    ToggleSetup toggle;

    bool operator==(const ProfileSetup&) const = default;
};

namespace spec_detail {

// Smallest width that represents 0..maximum, or a signed -maximum..maximum.
inline uint32_t unsigned_bits(const int64_t maximum) noexcept {
    uint32_t bits = 1;
    while (bits < 32 && (int64_t{1} << bits) - 1 < maximum)
        ++bits;
    return bits;
}

inline aoahid_integer_field field(const int32_t minimum, const int32_t maximum,
                                  const uint32_t bits) noexcept {
    aoahid_integer_field value{};
    value.logical_minimum = minimum;
    value.logical_maximum = maximum;
    value.bit_width = bits;
    return value;
}

struct AxisIdentity {
    std::string_view name;
    aoahid_axis_role role;
    uint16_t usage_page;
    uint16_t usage;
    const char* linux_code;
    const char* android_axis;
};

// Usage Page/Usage are the audited HUT 1.7 identities libaoahid re-checks. The
// Linux codes follow hidinput_configure_usage's Generic Desktop `usage & 0xf`
// and Simulation Controls mappings; the Android names are their MotionEvent
// counterparts. libaoahid stores both as required target evidence.
inline constexpr std::array<AxisIdentity, 14> axis_table{{
    {"x", AOAHID_AXIS_X, 0x01, 0x30, "ABS_X", "AXIS_X"},
    {"y", AOAHID_AXIS_Y, 0x01, 0x31, "ABS_Y", "AXIS_Y"},
    {"z", AOAHID_AXIS_Z, 0x01, 0x32, "ABS_Z", "AXIS_Z"},
    {"rx", AOAHID_AXIS_RX, 0x01, 0x33, "ABS_RX", "AXIS_RX"},
    {"ry", AOAHID_AXIS_RY, 0x01, 0x34, "ABS_RY", "AXIS_RY"},
    {"rz", AOAHID_AXIS_RZ, 0x01, 0x35, "ABS_RZ", "AXIS_RZ"},
    {"slider", AOAHID_AXIS_SLIDER, 0x01, 0x36, "ABS_THROTTLE", "AXIS_THROTTLE"},
    {"dial", AOAHID_AXIS_DIAL, 0x01, 0x37, "ABS_RUDDER", "AXIS_RUDDER"},
    {"wheel", AOAHID_AXIS_WHEEL, 0x01, 0x38, "ABS_WHEEL", "AXIS_WHEEL"},
    {"rudder", AOAHID_AXIS_SIMULATION_RUDDER, 0x02, 0xBA, "ABS_RUDDER", "AXIS_RUDDER"},
    {"throttle", AOAHID_AXIS_SIMULATION_THROTTLE, 0x02, 0xBB, "ABS_THROTTLE", "AXIS_THROTTLE"},
    {"accelerator", AOAHID_AXIS_SIMULATION_ACCELERATOR, 0x02, 0xC4, "ABS_GAS", "AXIS_GAS"},
    {"brake", AOAHID_AXIS_SIMULATION_BRAKE, 0x02, 0xC5, "ABS_BRAKE", "AXIS_BRAKE"},
    {"steering", AOAHID_AXIS_SIMULATION_STEERING, 0x02, 0xC8, "ABS_WHEEL", "AXIS_WHEEL"},
}};

inline const AxisIdentity* find_axis(const std::string_view name) noexcept {
    for (const AxisIdentity& entry : axis_table) {
        if (entry.name == name)
            return &entry;
    }
    return nullptr;
}

inline const AxisIdentity* find_axis(const aoahid_axis_role role) noexcept {
    for (const AxisIdentity& entry : axis_table) {
        if (entry.role == role)
            return &entry;
    }
    return nullptr;
}

// Accelerator/Brake/Throttle need 0..max with neutral 0; Steering/Rudder need
// a signed interval around zero. Every other role keeps the signed stick range.
inline bool axis_is_unipolar(const aoahid_axis_role role) noexcept {
    return role == AOAHID_AXIS_SIMULATION_ACCELERATOR || role == AOAHID_AXIS_SIMULATION_BRAKE ||
           role == AOAHID_AXIS_SIMULATION_THROTTLE;
}

} // namespace spec_detail

inline bool profile_enabled(const ProfileSetup& setup, const Profile profile) noexcept {
    switch (profile) {
    case Profile::touch:
        return setup.touch.enabled;
    case Profile::mouse:
        return setup.mouse.enabled;
    case Profile::key:
        return setup.key.enabled;
    case Profile::gamepad:
        return setup.gamepad.enabled;
    case Profile::pen:
        return setup.pen.enabled;
    case Profile::digitizer:
        return setup.digitizer.enabled;
    case Profile::toggle:
        return setup.toggle.enabled;
    default:
        return false;
    }
}

inline void set_profile_enabled(ProfileSetup& setup, const Profile profile,
                                const bool enabled) noexcept {
    switch (profile) {
    case Profile::touch:
        setup.touch.enabled = enabled;
        break;
    case Profile::mouse:
        setup.mouse.enabled = enabled;
        break;
    case Profile::key:
        setup.key.enabled = enabled;
        break;
    case Profile::gamepad:
        setup.gamepad.enabled = enabled;
        break;
    case Profile::pen:
        setup.pen.enabled = enabled;
        break;
    case Profile::digitizer:
        setup.digitizer.enabled = enabled;
        break;
    case Profile::toggle:
        setup.toggle.enabled = enabled;
        break;
    default:
        break;
    }
}

// Whether one profile is set up the same way in both, its switch included.
inline bool same_profile(const ProfileSetup& a, const ProfileSetup& b,
                         const Profile profile) noexcept {
    switch (profile) {
    case Profile::touch:
        return a.touch == b.touch;
    case Profile::mouse:
        return a.mouse == b.mouse;
    case Profile::key:
        return a.key == b.key;
    case Profile::gamepad:
        return a.gamepad == b.gamepad;
    case Profile::pen:
        return a.pen == b.pen;
    case Profile::digitizer:
        return a.digitizer == b.digitizer;
    case Profile::toggle:
        return a.toggle == b.toggle;
    default:
        return true;
    }
}

inline uint32_t enabled_profiles(const ProfileSetup& setup) noexcept {
    uint32_t mask = 0;
    for (size_t index = 0; index < profile_count; ++index) {
        const Profile profile = static_cast<Profile>(index);
        if (profile_enabled(setup, profile))
            mask |= profile_bit(profile);
    }
    return mask;
}

// The CSV pen and digitizer rows have no resolution column, so they share the
// touch surface when one is set and otherwise use a unitless 0..32767 square
// the target maps onto its own screen.
inline void resolve_pen_surface(ProfileSetup& setup) noexcept {
    const bool touch = setup.touch.enabled && setup.touch.width > 0 && setup.touch.height > 0;
    setup.pen.width = touch ? setup.touch.width : 32768;
    setup.pen.height = touch ? setup.touch.height : 32768;
    setup.digitizer.width = setup.pen.width;
    setup.digitizer.height = setup.pen.height;
}

// Checks one profile's settings, whether or not it is switched on. Returns
// false with a message a user can act on; libaoahid still performs its own
// full checks.
inline bool validate_profile(const ProfileSetup& setup, const Profile profile,
                             std::string& error) {
    // The int32_t width/height already tops out at INT32_MAX (2147483647), not
    // 2^32: a HID Logical Maximum item is always signed (HID 1.11 6.2.2.7),
    // matching both aoahid_integer_field.logical_maximum (int32_t) and the
    // Linux kernel's struct input_absinfo.maximum (__s32) that the eventual
    // ABS_MT_POSITION_X/Y range lands in.
    const auto size_ok = [](const int32_t value) { return value > 0; };
    switch (profile) {
    case Profile::touch:
        if (!size_ok(setup.touch.width) || !size_ok(setup.touch.height)) {
            error = "Set the touchscreen resolution (width and height, 1-2147483647).";
            return false;
        }
        if (setup.touch.max_contacts < 1 || setup.touch.max_contacts > 16) {
            error = "Touchscreen contacts must be between 1 and 16.";
            return false;
        }
        return true;
    case Profile::mouse:
        if (setup.mouse.buttons < 1 || setup.mouse.buttons > 65535) {
            error = "Mouse buttons must be between 1 and 65535.";
            return false;
        }
        return true;
    case Profile::key:
        if (setup.key.usage_minimum < 0x04 ||
            setup.key.usage_maximum < setup.key.usage_minimum ||
            setup.key.usage_maximum >= 0xE0) {
            error = "The keyboard usage range must be ordered, start at 0x04 or above and end "
                    "below 0xE0 (0xE0-0xE7 are the modifier keys).";
            return false;
        }
        return true;
    case Profile::gamepad: {
        const GamepadSetup& pad = setup.gamepad;
        if (pad.buttons < 1 || pad.buttons > 65535) {
            error = "Gamepad buttons must be between 1 and 65535.";
            return false;
        }
        if (pad.axis_bits < 2 || pad.axis_bits > 32) {
            error = "Gamepad axis resolution must be between 2 and 32 bits.";
            return false;
        }
        bool has_x = false;
        bool has_y = false;
        for (size_t index = 0; index < pad.axes.size(); ++index) {
            has_x = has_x || pad.axes[index] == AOAHID_AXIS_X;
            has_y = has_y || pad.axes[index] == AOAHID_AXIS_Y;
            for (size_t other = 0; other < index; ++other) {
                if (pad.axes[other] == pad.axes[index]) {
                    error = "Each gamepad axis can only be listed once.";
                    return false;
                }
            }
        }
        if (!has_x || !has_y) {
            error = "The gamepad needs at least the X and Y axes.";
            return false;
        }
        return true;
    }
    case Profile::pen:
        if (!size_ok(setup.pen.width) || !size_ok(setup.pen.height)) {
            error = "The pen surface size is not set.";
            return false;
        }
        return true;
    case Profile::digitizer:
        if (!size_ok(setup.digitizer.width) || !size_ok(setup.digitizer.height)) {
            error = "The digitizer surface size is not set.";
            return false;
        }
        return true;
    default:
        return true;
    }
}

// Checks every enabled profile before any USB work. A setup with no profile
// is valid: the device is connected and receives nothing.
inline bool validate_setup(const ProfileSetup& setup, std::string& error) {
    for (size_t index = 0; index < profile_count; ++index) {
        const Profile profile = static_cast<Profile>(index);
        if (profile_enabled(setup, profile) && !validate_profile(setup, profile, error))
            return false;
    }
    return true;
}

namespace spec_detail {

inline aoahid_result create_touch(const TouchSetup& setup, aoahid_spec** spec) {
    aoahid_touchscreen_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    options.maximum_contacts = setup.max_contacts;
    // One report carries every declared slot, so a frame is never split
    // across control transfers.
    options.contacts_per_report = setup.max_contacts;
    // Four bits cover the 0..15 Contact Identifier space that
    // maximum_contacts is itself capped at.
    options.contact_id = field(0, 15, 4);
    options.x = field(0, setup.width - 1, unsigned_bits(setup.width - 1));
    options.y = field(0, setup.height - 1, unsigned_bits(setup.height - 1));
    options.contact_count = field(0, static_cast<int32_t>(setup.max_contacts),
                                  unsigned_bits(static_cast<int64_t>(setup.max_contacts)));
    // The CSV touch row carries no pressure, geometry, or scan time, so
    // those optional fields stay out of the descriptor entirely.
    return aoahid_spec_create_touchscreen(&options, spec);
}

inline aoahid_result create_mouse(const MouseSetup& setup, aoahid_spec** spec) {
    aoahid_mouse_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    options.button_count = setup.buttons;
    // Relative deltas; libaoahid keeps a 64-bit pending total and emits
    // in-range fragments, so this range bounds one report, not one script
    // row. The wheel serves live control; AC Pan stays undeclared.
    options.x = field(-32767, 32767, 16);
    options.y = field(-32767, 32767, 16);
    options.enable_wheel = 1U;
    options.wheel = field(-127, 127, 8);
    return aoahid_spec_create_mouse(&options, spec);
}

inline aoahid_result create_key(const KeySetup& setup, aoahid_spec** spec) {
    aoahid_keyboard_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    // The keyboard profile is exclusively full-NKRO as of libaoahid 0.2.0:
    // every usage is a one-bit Variable field, so there is no Array slot
    // count or ErrorRollOver encoding left to configure.
    options.usage_minimum = setup.usage_minimum;
    options.usage_maximum = setup.usage_maximum;
    return aoahid_spec_create_keyboard(&options, spec);
}

inline aoahid_result create_gamepad(const GamepadSetup& setup, aoahid_spec** spec) {
    const uint32_t bits = setup.axis_bits;
    const int32_t signed_extent = static_cast<int32_t>((int64_t{1} << (bits - 1)) - 1);
    // A 32-bit unipolar axis would need 2^32-1, which no longer fits the
    // signed logical maximum, so it saturates at INT32_MAX.
    const int64_t unsigned_span = (int64_t{1} << bits) - 1;
    const int32_t unsigned_extent = static_cast<int32_t>(
        unsigned_span > int64_t{INT32_MAX} ? int64_t{INT32_MAX} : unsigned_span);

    std::vector<aoahid_gamepad_axis_option> axes;
    axes.reserve(setup.axes.size());
    for (const aoahid_axis_role role : setup.axes) {
        const AxisIdentity* identity = find_axis(role);
        if (identity == nullptr)
            return AOAHID_ERR_PARAM;
        aoahid_gamepad_axis_option axis{};
        axis.role = identity->role;
        axis.usage_page = identity->usage_page;
        axis.usage = identity->usage;
        axis.value = axis_is_unipolar(role) ? field(0, unsigned_extent, bits)
                                            : field(-signed_extent, signed_extent, bits);
        axis.neutral_value = 0;
        axis.expected_linux_code = identity->linux_code;
        axis.expected_android_axis = identity->android_axis;
        axes.push_back(axis);
    }

    aoahid_gamepad_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    // libaoahid 0.3.0 removed the Joystick Application Collection and the
    // options.application selector; aoahid_spec_create_gamepad now always
    // emits the Generic Desktop / Game Pad form.
    options.axes = axes.data();
    options.axis_count = axes.size();
    options.button_count = setup.buttons;
    options.button_usage_minimum = 1U;
    options.dpad_representation = setup.dpad;
    if (setup.dpad == AOAHID_DPAD_HAT) {
        // The Android portable-candidate Hat is fixed at logical 0..7 in
        // four bits, leaving 15 as the Null State encoding.
        options.hat_logical_minimum = 0;
        options.hat_logical_maximum = 7;
        options.hat_bit_width = 4U;
    }
    return aoahid_spec_create_gamepad(&options, spec);
}

inline aoahid_result create_pen(const PenSetup& setup, aoahid_spec** spec) {
    aoahid_pen_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    options.x = field(0, setup.width - 1, unsigned_bits(setup.width - 1));
    options.y = field(0, setup.height - 1, unsigned_bits(setup.height - 1));
    options.enable_pressure = 1U;
    options.pressure =
        field(0, setup.pressure_maximum, unsigned_bits(setup.pressure_maximum));
    // Hover must be declared, or libaoahid rejects the in_range=1, tip=0
    // sample the CSV pen row is able to express.
    options.enable_hover = 1U;
    return aoahid_spec_create_pen(&options, spec);
}

inline aoahid_result create_digitizer(const DigitizerSetup& setup, aoahid_spec** spec) {
    aoahid_digitizer_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    options.x = field(0, setup.width - 1, unsigned_bits(setup.width - 1));
    options.y = field(0, setup.height - 1, unsigned_bits(setup.height - 1));
    options.enable_pressure = 1U;
    options.pressure =
        field(0, setup.pressure_maximum, unsigned_bits(setup.pressure_maximum));
    // Hover must be declared, or libaoahid rejects the in_range=1, tip=0
    // sample the CSV digitizer row is able to express.
    options.enable_hover = 1U;
    return aoahid_spec_create_digitizer(&options, spec);
}

inline aoahid_result create_toggle(aoahid_spec** spec) {
    aoahid_toggle_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    options.application_page = 0x0CU; // Consumer
    options.application_usage = 0x01U; // Consumer Control
    options.field_page = 0x0CU;

    // media_usages (key_names.hpp) is the one list the CSV parser also
    // checks media key rows against; the arrays below follow its order:
    // Next, Prev, Stop, Play/Pause, Mute, Vol Up, Vol Down, AC Home,
    // AC Back, AC Pan, AC New, Brightness Up, Brightness Down. Brightness
    // Increment/Decrement are RTC (HUT 1.5, 15.5).
    const auto& usages = media_usages;
    static const aoahid_usage_semantic semantics[] = {
        AOAHID_USAGE_ONE_SHOT,          // Next
        AOAHID_USAGE_ONE_SHOT,          // Prev
        AOAHID_USAGE_ONE_SHOT,          // Stop
        AOAHID_USAGE_ONE_SHOT,          // Play/Pause
        AOAHID_USAGE_ON_OFF_MAINTAINED, // Mute
        AOAHID_USAGE_RETRIGGER,         // Vol Up
        AOAHID_USAGE_RETRIGGER,         // Vol Down
        AOAHID_USAGE_ONE_SHOT,          // AC Home
        AOAHID_USAGE_ONE_SHOT,          // AC Back
        AOAHID_USAGE_ONE_SHOT,          // AC Pan
        AOAHID_USAGE_SELECTOR_BITMAP,   // AC New
        AOAHID_USAGE_RETRIGGER,         // Brightness Up
        AOAHID_USAGE_RETRIGGER          // Brightness Down
    };
    // Linux hid-input maps AC Pan to the horizontal wheel, not to a key.
    static const char* expected_types[] = {
        "EV_KEY", "EV_KEY", "EV_KEY", "EV_KEY", "EV_KEY",
        "EV_KEY", "EV_KEY", "EV_KEY", "EV_KEY", "EV_REL", "EV_KEY",
        "EV_KEY", "EV_KEY"
    };
    static const char* expected_codes[] = {
        "KEY_NEXTSONG", "KEY_PREVIOUSSONG", "KEY_STOPCD", "KEY_PLAYPAUSE", "KEY_MUTE",
        "KEY_VOLUMEUP", "KEY_VOLUMEDOWN", "KEY_HOMEPAGE", "KEY_BACK", "REL_HWHEEL", "KEY_NEW",
        "KEY_BRIGHTNESSUP", "KEY_BRIGHTNESSDOWN"
    };
    static_assert(std::size(semantics) == std::size(usages) &&
                  std::size(expected_types) == std::size(usages) &&
                  std::size(expected_codes) == std::size(usages));

    options.allowed_usages = usages;
    options.allowed_usage_count = std::size(usages);
    options.usage_semantics = semantics;
    options.expected_linux_event_types = expected_types;
    options.expected_linux_codes = expected_codes;

    return aoahid_spec_create_toggle(&options, spec);
}

} // namespace spec_detail

// Builds the Spec of one profile. The Node keeps its own reference, so the
// caller releases `spec` once the Node is open.
inline bool build_spec(const Profile profile, const ProfileSetup& setup, aoahid_spec*& spec,
                       std::string& error) {
    spec = nullptr;
    aoahid_result result = AOAHID_ERR_PARAM;
    switch (profile) {
    case Profile::touch:
        result = spec_detail::create_touch(setup.touch, &spec);
        break;
    case Profile::mouse:
        result = spec_detail::create_mouse(setup.mouse, &spec);
        break;
    case Profile::key:
        result = spec_detail::create_key(setup.key, &spec);
        break;
    case Profile::gamepad:
        result = spec_detail::create_gamepad(setup.gamepad, &spec);
        break;
    case Profile::pen:
        result = spec_detail::create_pen(setup.pen, &spec);
        break;
    case Profile::digitizer:
        result = spec_detail::create_digitizer(setup.digitizer, &spec);
        break;
    case Profile::toggle:
        result = spec_detail::create_toggle(&spec);
        break;
    default:
        break;
    }
    if (result != AOAHID_OK) {
        spec = nullptr;
        error = "The " + describe_profiles(profile_bit(profile)) +
                " settings were rejected: " + explain_error(result);
        return false;
    }
    return true;
}

} // namespace aoap
