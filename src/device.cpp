// SPDX-License-Identifier: MIT
#include "aoahid_player/device.hpp"

#include "aoahid_player/context.hpp"

#include <aoahid_adb_proxy.h>

#include <cstdio>
#include <utility>
#include <vector>

namespace aoap {
namespace {

// Every tuning value below is this application's policy, not a libaoahid
// default. Zero-valued fields would select the fallbacks documented in
// libaoahid's docs/API.md; they are stated explicitly so the descriptor and
// transport budgets are visible in one place.
constexpr uint32_t control_timeout_ms = 500U;
constexpr uint32_t send_timeout_ms = 500U;
constexpr uint32_t descriptor_policy_bytes = 4096U;
constexpr uint32_t report_policy_bytes = 1024U;
constexpr uint32_t pool_slots = 8U;
constexpr uint32_t close_drain_timeout_ms = 1000U;

aoahid_device_options make_device_options() noexcept {
    aoahid_device_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    options.control_timeout_ms = control_timeout_ms;
    options.send_timeout_ms = send_timeout_ms;
    options.descriptor_fragment_bytes = descriptor_policy_bytes;
    options.transfer_pool_slots = pool_slots;
    options.maximum_report_bytes = report_policy_bytes;
    options.close_drain_timeout_ms = close_drain_timeout_ms;
    // The specs this player builds are fixed and already range-checked by the
    // profile factories, so the second wire-image scan is not run per report.
    options.validate_reports = 0U;
    options.aoa_descriptor_wire_policy_bytes = descriptor_policy_bytes;
    options.linux_descriptor_policy_bytes = descriptor_policy_bytes;
    // These five are the Linux HID parser limits listed in libaoahid's
    // docs/LIMITS.md.
    options.linux_hid_fields_per_report_policy = 256U;
    options.linux_hid_global_stack_depth_policy = 4U;
    options.linux_hid_usages_policy = 12288U;
    options.linux_hid_report_data_bits_policy = 65528U;
    options.linux_hid_report_size_bits_policy = 256U;
    options.target_ep0_data_policy_bytes = descriptor_policy_bytes;
    options.host_control_buffer_policy_bytes = descriptor_policy_bytes;
    options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    options.interface_number = -1;
    return options;
}

aoahid_node_options make_node_options() noexcept {
    aoahid_node_options options{};
    options.struct_size = static_cast<uint32_t>(sizeof(options));
    // A Node has one report in flight, so one reserved slot removes pool
    // contention between the profiles sharing this Device.
    options.has_reserved_slots = 1U;
    options.reserved_slots = 1U;
    return options;
}

} // namespace

const char* profile_name(const Profile profile) noexcept {
    switch (profile) {
    case Profile::touch:
        return "touch";
    case Profile::mouse:
        return "mouse";
    case Profile::key:
        return "key";
    case Profile::gamepad:
        return "gamepad";
    case Profile::pen:
        return "pen";
    case Profile::toggle:
        return "toggle";
    default:
        return "unknown";
    }
}

const char* profile_title(const Profile profile) noexcept {
    switch (profile) {
    case Profile::touch:
        return "Touchscreen";
    case Profile::mouse:
        return "Mouse";
    case Profile::key:
        return "Keyboard";
    case Profile::gamepad:
        return "Gamepad";
    case Profile::pen:
        return "Pen";
    case Profile::toggle:
        return "Toggle Controls";
    default:
        return "Unknown";
    }
}

std::string describe_profiles(const uint32_t mask) {
    std::vector<std::string> names;
    for (size_t index = 0; index < profile_count; ++index) {
        if ((mask & (uint32_t{1} << index)) == 0U)
            continue;
        std::string name = profile_title(static_cast<Profile>(index));
        for (char& c : name)
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        names.push_back(std::move(name));
    }
    std::string text;
    for (size_t index = 0; index < names.size(); ++index) {
        if (index != 0)
            text += index + 1 == names.size() ? " and " : ", ";
        text += names[index];
    }
    return text.empty() ? std::string("no profile") : text;
}

std::string device_label(const aoahid_device_info* info) {
    if (info == nullptr)
        return "(unknown device)";
    char identity[32];
    std::snprintf(identity, sizeof identity, "%04x:%04x", static_cast<unsigned>(info->vendor_id),
                  static_cast<unsigned>(info->product_id));
    std::string text = info->product != nullptr && info->product[0] != '\0' ? info->product
                                                                            : "(no product name)";
    text += "  ";
    text += identity;
    text += "  ";
    text += info->serial != nullptr && info->serial[0] != '\0' ? info->serial : "(no serial)";
    return text;
}

std::string explain_adb_bridge_error(const int code, const uint16_t port) {
    const std::string at = "127.0.0.1:" + std::to_string(port);
    switch (code) {
    case AOAHID_ADB_PROXY_ERR_INTERFACE: {
        // The proxy makes no libaoahid call after a failed aoahid_channel_open,
        // so this thread's last diagnostic is that failure.
        const aoahid_error_detail* detail = aoahid_last_error();
        const aoahid_result result = static_cast<aoahid_result>(detail->code);
        const char* hint =
            ". USB debugging may be off, or another program holds the interface";
#ifdef _WIN32
        // LIBUSB_ERROR_NOT_SUPPORTED: libusb cannot use the interface's driver.
        if (detail->libusb_status == -12)
            hint = ". The phone's adb interface needs a driver libusb can use, such as WinUSB "
                   "(see Troubleshooting in README.md)";
#endif
        const std::string usb_status = std::to_string(detail->libusb_status);
        return "the phone's ADB interface could not be opened: " + describe_error(result) +
               ", libusb status " + usb_status + hint;
    }
    case AOAHID_ADB_PROXY_ERR_BIND:
        return "port " + std::to_string(port) + " is already in use; choose another port";
    case AOAHID_ADB_PROXY_ERR_SOCKET:
    case AOAHID_ADB_PROXY_ERR_LISTEN:
        return "could not listen on " + at;
    case AOAHID_ADB_PROXY_ERR_RESOURCE:
        return "out of memory, or no thread could be started";
    default:
        return "failed (code " + std::to_string(code) + ")";
    }
}

bool adb_bridge_interface_unavailable(const int code) noexcept {
    return code == AOAHID_ADB_PROXY_ERR_INTERFACE;
}

Device::~Device() { close(); }

Device::Device(Device&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      adb_bridge_(std::exchange(other.adb_bridge_, nullptr)),
      adb_port_(std::exchange(other.adb_port_, uint16_t{0})), nodes_(other.nodes_),
      profile_mask_(std::exchange(other.profile_mask_, 0U)), label_(std::move(other.label_)) {
    other.nodes_.fill(nullptr);
}

Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        adb_bridge_ = std::exchange(other.adb_bridge_, nullptr);
        adb_port_ = std::exchange(other.adb_port_, uint16_t{0});
        nodes_ = other.nodes_;
        profile_mask_ = std::exchange(other.profile_mask_, 0U);
        label_ = std::move(other.label_);
        other.nodes_.fill(nullptr);
    }
    return *this;
}

aoahid_result Device::open(const Context& context, const aoahid_device_info* selected) {
    close();
    if (context.native_handle() == nullptr || selected == nullptr)
        return AOAHID_ERR_PARAM;
    label_ = device_label(selected);
    const aoahid_device_options options = make_device_options();
    return aoahid_device_open(context.native_handle(), selected, &options, &handle_);
}

aoahid_result Device::open_node(const Profile profile, aoahid_spec* spec) noexcept {
    if (handle_ == nullptr || spec == nullptr)
        return AOAHID_ERR_PARAM;
    const size_t index = static_cast<size_t>(profile);
    if (nodes_[index] != nullptr)
        return AOAHID_ERR_PARAM;

    const aoahid_node_options options = make_node_options();
    aoahid_node* opened_node = nullptr;
    const aoahid_result opened = aoahid_node_open(handle_, spec, &options, &opened_node);
    if (opened != AOAHID_OK)
        return opened;

    nodes_[index] = opened_node;
    profile_mask_ |= profile_bit(profile);
    return AOAHID_OK;
}

int Device::start_adb_bridge(const uint16_t port) noexcept {
    if (handle_ == nullptr || adb_bridge_ != nullptr)
        return AOAHID_ADB_PROXY_ERR_ARGUMENT;
    const int result = aoahid_adb_proxy_start(handle_, port, &adb_bridge_);
    if (result == AOAHID_ADB_PROXY_OK)
        adb_port_ = port;
    else
        adb_bridge_ = nullptr;
    return result;
}

void Device::stop_adb_bridge() noexcept {
    // Joins the proxy threads and closes its Channel; neither may overlap
    // aoahid_device_close.
    aoahid_adb_proxy_stop(adb_bridge_);
    adb_bridge_ = nullptr;
    adb_port_ = 0U;
}

void Device::close() noexcept {
    stop_adb_bridge();
    // The first aoahid_device_close consumes the Device and every child Node,
    // including on AOAHID_CLOSE_PENDING, so the Nodes are not closed here.
    if (handle_ != nullptr)
        static_cast<void>(aoahid_device_close(handle_));
    handle_ = nullptr;
    nodes_.fill(nullptr);
    profile_mask_ = 0U;
}

} // namespace aoap
