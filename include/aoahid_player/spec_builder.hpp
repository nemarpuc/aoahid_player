// SPDX-License-Identifier: MIT
#pragma once

#include <aoahid.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "context.hpp"
#include "device.hpp"

// Turns the chosen profile settings into the immutable aoahid_spec objects a
// connection uses. Specs are built once when connecting and reused for every
// Node on every device; nothing here runs inside the playback loop.

namespace aoap {

struct TouchSetup {
    bool enabled{};
    int32_t width{};
    int32_t height{};
    uint32_t max_contacts{};
};

struct MouseSetup {
    bool enabled{};
    uint32_t buttons{};
};

struct KeySetup {
    bool enabled{};
    uint16_t usage_minimum{0x04};
    uint16_t usage_maximum{0x65};
};

struct GamepadSetup {
    bool enabled{};
    uint32_t buttons{};
    uint32_t axis_bits{};
    aoahid_dpad_representation dpad{AOAHID_DPAD_NONE};
    std::vector<aoahid_axis_role> axes;
};

struct PenSetup {
    bool enabled{};
    aoahid_pen_mode mode{AOAHID_PEN_DIRECT_SCREEN};
    int32_t width{};
    int32_t height{};
    int32_t pressure_maximum{4095};
};

// Media keys (Consumer page): volume up/down, mute, play/pause, next/
// previous track, stop. A fixed set — see spec_detail::consumer_usages —
// rather than a caller-chosen range like the keyboard profile, since these
// are Live/API buttons, not something a CSV script declares a range for.
struct ConsumerSetup {
    bool enabled{};
};

// Power Down, Sleep, Wake Up (System Control page): a second, independent
// fixed set from ConsumerSetup — see spec_detail::system_table — since HUT
// System Control is a different Application Collection from Consumer
// Control and libaoahid speaks one per Spec/Node.
struct SystemSetup {
    bool enabled{};
};

struct ProfileSetup {
    TouchSetup touch;
    MouseSetup mouse;
    KeySetup key;
    GamepadSetup gamepad;
    PenSetup pen;
    ConsumerSetup consumer;
    SystemSetup system;
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

// HUT 0x0C (Consumer) usages this player exposes, with the Linux evdev
// mapping libaoahid requires as evidence (drivers/hid/hid-input.c's
// consumer-page table) and the HUT-defined report semantic for each:
// Volume Increment/Decrement are Re-trigger Controls (repeat while held on a
// physical remote); the rest are One Shot Controls.
inline constexpr uint16_t consumer_usage_volume_up = 0xE9;
inline constexpr uint16_t consumer_usage_volume_down = 0xEA;
inline constexpr uint16_t consumer_usage_mute = 0xE2;
inline constexpr uint16_t consumer_usage_play_pause = 0xCD;
inline constexpr uint16_t consumer_usage_next_track = 0xB5;
inline constexpr uint16_t consumer_usage_previous_track = 0xB6;
inline constexpr uint16_t consumer_usage_stop = 0xB7;

struct ConsumerIdentity {
    std::string_view name; // "Volume Up", ...
    uint16_t usage;
    aoahid_usage_semantic semantic;
    const char* linux_code;
};

inline constexpr std::array<ConsumerIdentity, 7> consumer_table{{
    {"Volume Up", consumer_usage_volume_up, AOAHID_USAGE_RETRIGGER, "KEY_VOLUMEUP"},
    {"Volume Down", consumer_usage_volume_down, AOAHID_USAGE_RETRIGGER, "KEY_VOLUMEDOWN"},
    {"Mute", consumer_usage_mute, AOAHID_USAGE_ONE_SHOT, "KEY_MUTE"},
    {"Play/Pause", consumer_usage_play_pause, AOAHID_USAGE_ONE_SHOT, "KEY_PLAYPAUSE"},
    {"Previous Track", consumer_usage_previous_track, AOAHID_USAGE_ONE_SHOT, "KEY_PREVIOUSSONG"},
    {"Next Track", consumer_usage_next_track, AOAHID_USAGE_ONE_SHOT, "KEY_NEXTSONG"},
    {"Stop", consumer_usage_stop, AOAHID_USAGE_ONE_SHOT, "KEY_STOPCD"},
}};

// HUT 0x01/0x80 (System Control) usages this player exposes, same shape and
// reasoning as consumer_table; all three are One Shot Controls.
inline constexpr uint16_t system_usage_power_down = 0x81;
inline constexpr uint16_t system_usage_sleep = 0x82;
inline constexpr uint16_t system_usage_wake_up = 0x83;

// Reuses ConsumerIdentity's shape: name/usage/semantic/evidence is exactly
// what a toggle_node_ref usage needs, regardless of which HUT page it is on.
using SystemIdentity = ConsumerIdentity;

inline constexpr std::array<SystemIdentity, 3> system_table{{
    {"Power", system_usage_power_down, AOAHID_USAGE_ONE_SHOT, "KEY_POWER"},
    {"Sleep", system_usage_sleep, AOAHID_USAGE_ONE_SHOT, "KEY_SLEEP"},
    {"Wake Up", system_usage_wake_up, AOAHID_USAGE_ONE_SHOT, "KEY_WAKEUP"},
}};

} // namespace spec_detail

inline uint32_t enabled_profiles(const ProfileSetup& setup) noexcept {
    uint32_t mask = 0;
    if (setup.touch.enabled)
        mask |= profile_bit(Profile::touch);
    if (setup.mouse.enabled)
        mask |= profile_bit(Profile::mouse);
    if (setup.key.enabled)
        mask |= profile_bit(Profile::key);
    if (setup.gamepad.enabled)
        mask |= profile_bit(Profile::gamepad);
    if (setup.pen.enabled)
        mask |= profile_bit(Profile::pen);
    if (setup.consumer.enabled)
        mask |= profile_bit(Profile::consumer);
    if (setup.system.enabled)
        mask |= profile_bit(Profile::system);
    return mask;
}

// The CSV pen row has no resolution column, so the pen shares the touch
// surface when one is set and otherwise uses a unitless 0..32767 square the
// target maps onto its own screen.
inline void resolve_pen_surface(ProfileSetup& setup) noexcept {
    const bool touch = setup.touch.enabled && setup.touch.width > 0 && setup.touch.height > 0;
    setup.pen.width = touch ? setup.touch.width : 32768;
    setup.pen.height = touch ? setup.touch.height : 32768;
}

// Checks every enabled profile before any USB work. Returns false with a
// message a user can act on; libaoahid still performs its own full checks.
inline bool validate_setup(const ProfileSetup& setup, std::string& error) {
    if (enabled_profiles(setup) == 0U) {
        error = "Choose at least one profile to connect.";
        return false;
    }
    const auto size_ok = [](const int32_t value) { return value > 0 && value <= 65536; };
    if (setup.touch.enabled) {
        if (!size_ok(setup.touch.width) || !size_ok(setup.touch.height)) {
            error = "Set the touchscreen resolution (width and height, 1-65536).";
            return false;
        }
        if (setup.touch.max_contacts < 1 || setup.touch.max_contacts > 16) {
            error = "Touchscreen contacts must be between 1 and 16.";
            return false;
        }
    }
    if (setup.mouse.enabled && (setup.mouse.buttons < 1 || setup.mouse.buttons > 65535)) {
        error = "Mouse buttons must be between 1 and 65535.";
        return false;
    }
    if (setup.key.enabled &&
        (setup.key.usage_minimum < 0x04 || setup.key.usage_maximum < setup.key.usage_minimum ||
         setup.key.usage_maximum >= 0xE0)) {
        error = "The keyboard usage range must be ordered, start at 0x04 or above and end "
                "below 0xE0 (0xE0-0xE7 are the modifier keys).";
        return false;
    }
    if (setup.gamepad.enabled) {
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
    }
    if (setup.pen.enabled && (!size_ok(setup.pen.width) || !size_ok(setup.pen.height))) {
        error = "The pen surface size is not set.";
        return false;
    }
    return true;
}

// Owns one immutable Spec per enabled profile for a whole connection.
class SpecSet {
  public:
    SpecSet() noexcept = default;
    ~SpecSet() { release(); }

    SpecSet(const SpecSet&) = delete;
    SpecSet& operator=(const SpecSet&) = delete;
    SpecSet(SpecSet&& other) noexcept : specs_(other.specs_) { other.specs_.fill(nullptr); }
    SpecSet& operator=(SpecSet&& other) noexcept {
        if (this != &other) {
            release();
            specs_ = other.specs_;
            other.specs_.fill(nullptr);
        }
        return *this;
    }

    [[nodiscard]] aoahid_spec* get(const Profile profile) const noexcept {
        return specs_[static_cast<size_t>(profile)];
    }
    [[nodiscard]] uint32_t mask() const noexcept {
        uint32_t value = 0;
        for (size_t index = 0; index < profile_count; ++index) {
            if (specs_[index] != nullptr)
                value |= uint32_t{1} << index;
        }
        return value;
    }

    void release() noexcept {
        for (aoahid_spec*& spec : specs_) {
            aoahid_spec_release(spec);
            spec = nullptr;
        }
    }

    // Builds every enabled profile. Returns false and fills `error` on the
    // first factory rejection, leaving already-built specs owned by this set.
    bool build(const ProfileSetup& setup, std::string& error) {
        release();
        if (setup.touch.enabled && !build_touch(setup.touch, error))
            return false;
        if (setup.mouse.enabled && !build_mouse(setup.mouse, error))
            return false;
        if (setup.key.enabled && !build_key(setup.key, error))
            return false;
        if (setup.gamepad.enabled && !build_gamepad(setup.gamepad, error))
            return false;
        if (setup.pen.enabled && !build_pen(setup.pen, error))
            return false;
        if (setup.consumer.enabled && !build_consumer(setup.consumer, error))
            return false;
        if (setup.system.enabled && !build_system(setup.system, error))
            return false;
        return true;
    }

  private:
    std::array<aoahid_spec*, profile_count> specs_{};

    bool store(const Profile profile, const aoahid_result result, aoahid_spec* spec,
               const char* what, std::string& error) {
        if (result != AOAHID_OK) {
            error = std::string("The ") + what + " settings were rejected: " +
                    explain_error(result);
            return false;
        }
        specs_[static_cast<size_t>(profile)] = spec;
        return true;
    }

    bool build_touch(const TouchSetup& setup, std::string& error) {
        using namespace spec_detail;
        aoahid_touchscreen_options options{};
        options.struct_size = static_cast<uint32_t>(sizeof(options));
        options.maximum_contacts = setup.max_contacts;
        // One report carries every declared slot, so a frame is never split
        // across control transfers.
        options.contacts_per_report = setup.max_contacts;
        // Four bits cover the 0..15 Contact Identifier space that
        // maximum_contacts is itself capped at.
        options.contact_identifier = field(0, 15, 4);
        options.x = field(0, setup.width - 1, unsigned_bits(setup.width - 1));
        options.y = field(0, setup.height - 1, unsigned_bits(setup.height - 1));
        options.contact_count = field(0, static_cast<int32_t>(setup.max_contacts),
                                      unsigned_bits(static_cast<int64_t>(setup.max_contacts)));
        // The CSV touch row carries no pressure, geometry, or scan time, so
        // those optional fields stay out of the descriptor entirely.
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_touchscreen(&options, &spec);
        return store(Profile::touch, result, spec, "touchscreen", error);
    }

    bool build_mouse(const MouseSetup& setup, std::string& error) {
        using namespace spec_detail;
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
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_mouse(&options, &spec);
        return store(Profile::mouse, result, spec, "mouse", error);
    }

    bool build_key(const KeySetup& setup, std::string& error) {
        aoahid_keyboard_options options{};
        options.struct_size = static_cast<uint32_t>(sizeof(options));
        // The keyboard profile is exclusively full-NKRO as of libaoahid 0.2.0:
        // every usage is a one-bit Variable field, so there is no Array slot
        // count or ErrorRollOver encoding left to configure.
        options.usage_minimum = setup.usage_minimum;
        options.usage_maximum = setup.usage_maximum;
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_keyboard(&options, &spec);
        return store(Profile::key, result, spec, "keyboard", error);
    }

    bool build_gamepad(const GamepadSetup& setup, std::string& error) {
        using namespace spec_detail;
        const uint32_t bits = setup.axis_bits;
        const int32_t signed_extent = static_cast<int32_t>((int64_t{1} << (bits - 1)) - 1);
        // A 32-bit unipolar axis would need 2^32-1, which no longer fits the
        // signed logical maximum, so it saturates at INT32_MAX.
        const int64_t unsigned_span = (int64_t{1} << bits) - 1;
        const int32_t unsigned_extent = static_cast<int32_t>(
            unsigned_span > int64_t{INT32_MAX} ? int64_t{INT32_MAX} : unsigned_span);

        std::vector<aoahid_gamepad_axis> axes;
        axes.reserve(setup.axes.size());
        for (const aoahid_axis_role role : setup.axes) {
            const AxisIdentity* identity = find_axis(role);
            if (identity == nullptr) {
                error = "The gamepad settings use an unsupported axis.";
                return false;
            }
            aoahid_gamepad_axis axis{};
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
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_gamepad(&options, &spec);
        return store(Profile::gamepad, result, spec, "gamepad", error);
    }

    bool build_pen(const PenSetup& setup, std::string& error) {
        using namespace spec_detail;
        aoahid_pen_options options{};
        options.struct_size = static_cast<uint32_t>(sizeof(options));
        options.mode = setup.mode;
        options.x = field(0, setup.width - 1, unsigned_bits(setup.width - 1));
        options.y = field(0, setup.height - 1, unsigned_bits(setup.height - 1));
        options.enable_pressure = 1U;
        options.pressure =
            field(0, setup.pressure_maximum, unsigned_bits(setup.pressure_maximum));
        // Hover must be declared, or libaoahid rejects the in_range=1, tip=0
        // sample the CSV pen row is able to express.
        options.enable_hover = 1U;
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_pen(&options, &spec);
        return store(Profile::pen, result, spec, "pen", error);
    }

    bool build_consumer(const ConsumerSetup&, std::string& error) {
        using namespace spec_detail;
        std::array<uint16_t, consumer_table.size()> usages{};
        std::array<aoahid_usage_semantic, consumer_table.size()> semantics{};
        std::array<const char*, consumer_table.size()> linux_types{};
        std::array<const char*, consumer_table.size()> linux_codes{};
        for (size_t index = 0; index < consumer_table.size(); ++index) {
            const ConsumerIdentity& identity = consumer_table[index];
            usages[index] = identity.usage;
            semantics[index] = identity.semantic;
            linux_types[index] = "EV_KEY";
            linux_codes[index] = identity.linux_code;
        }

        aoahid_toggle_options options{};
        options.struct_size = static_cast<uint32_t>(sizeof(options));
        options.application_page = 0x0C; // Consumer
        options.application_usage = 0x01; // Consumer Control
        options.field_page = 0x0C;
        options.allowed_usages = usages.data();
        options.allowed_usage_count = usages.size();
        options.usage_semantics = semantics.data();
        options.expected_linux_event_types = linux_types.data();
        options.expected_linux_codes = linux_codes.data();
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_toggle(&options, &spec);
        return store(Profile::consumer, result, spec, "media key", error);
    }

    bool build_system(const SystemSetup&, std::string& error) {
        using namespace spec_detail;
        std::array<uint16_t, system_table.size()> usages{};
        std::array<aoahid_usage_semantic, system_table.size()> semantics{};
        std::array<const char*, system_table.size()> linux_types{};
        std::array<const char*, system_table.size()> linux_codes{};
        for (size_t index = 0; index < system_table.size(); ++index) {
            const SystemIdentity& identity = system_table[index];
            usages[index] = identity.usage;
            semantics[index] = identity.semantic;
            linux_types[index] = "EV_KEY";
            linux_codes[index] = identity.linux_code;
        }

        aoahid_toggle_options options{};
        options.struct_size = static_cast<uint32_t>(sizeof(options));
        options.application_page = 0x01; // Generic Desktop
        options.application_usage = 0x80; // System Control
        options.field_page = 0x01;
        options.allowed_usages = usages.data();
        options.allowed_usage_count = usages.size();
        options.usage_semantics = semantics.data();
        options.expected_linux_event_types = linux_types.data();
        options.expected_linux_codes = linux_codes.data();
        aoahid_spec* spec = nullptr;
        const aoahid_result result = aoahid_spec_create_toggle(&options, &spec);
        return store(Profile::system, result, spec, "system control", error);
    }
};

} // namespace aoap
