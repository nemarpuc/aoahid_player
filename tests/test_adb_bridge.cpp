// SPDX-License-Identifier: MIT
// Covers the ADB Bridge's host-side pieces: the adb helpers that decide when
// `adb connect` is needed and whether it worked, the bridge error sentences,
// and Device's refusal to start a bridge without an open device. Nothing
// here touches USB or runs adb.
#include "doctest.h"

#include "aoahid_player/adb.hpp"
#include "aoahid_player/device.hpp"

#include <aoahid_adb_proxy.h>

TEST_CASE("adb_bridge_address names the loopback port") {
    CHECK(aoap::adb_bridge_address(6555) == "127.0.0.1:6555");
    CHECK(aoap::adb_bridge_address(aoap::default_adb_bridge_port) == "127.0.0.1:6555");
}

TEST_CASE("adb_network_serial tells host:port from USB serials") {
    CHECK(aoap::adb_network_serial("127.0.0.1:6555"));
    CHECK(aoap::adb_network_serial("192.168.1.20:5555"));
    CHECK_FALSE(aoap::adb_network_serial(""));
    CHECK_FALSE(aoap::adb_network_serial("R58M123ABC"));
}

TEST_CASE("adb_connect_succeeded reads adb's own wording") {
    CHECK(aoap::adb_connect_succeeded("connected to 127.0.0.1:6555\n"));
    CHECK(aoap::adb_connect_succeeded("already connected to 127.0.0.1:6555\n"));
    // adb 37 exits 0 for all of these.
    CHECK_FALSE(aoap::adb_connect_succeeded(
        "failed to connect to '127.0.0.1:6555': Connection refused\n"));
    CHECK_FALSE(aoap::adb_connect_succeeded("cannot connect to 127.0.0.1:6555: timed out\n"));
    CHECK_FALSE(aoap::adb_connect_succeeded("failed to authenticate to 127.0.0.1:6555\n"));
    CHECK_FALSE(aoap::adb_connect_succeeded(""));
}

TEST_CASE("explain_adb_bridge_error names the cause") {
    CHECK(aoap::explain_adb_bridge_error(AOAHID_ADB_PROXY_ERR_INTERFACE, 6555).find("USB debugging") != std::string::npos);
    CHECK(aoap::explain_adb_bridge_error(AOAHID_ADB_PROXY_ERR_BIND, 6556).find("6556") != std::string::npos);
    CHECK(aoap::explain_adb_bridge_error(AOAHID_ADB_PROXY_ERR_LISTEN, 6555).find("127.0.0.1:6555") != std::string::npos);
}

TEST_CASE("the bridge does not start without an open device") {
    aoahid_adb_proxy_context* proxy = nullptr;
    CHECK(aoahid_adb_proxy_start(nullptr, 6555, &proxy) == AOAHID_ADB_PROXY_ERR_ARGUMENT);
    CHECK(proxy == nullptr);
    aoahid_adb_proxy_stop(nullptr); // ignored

    aoap::Device device;
    CHECK(device.start_adb_bridge(6555) == AOAHID_ADB_PROXY_ERR_ARGUMENT);
    CHECK(device.adb_port() == 0U);
    device.close();
    CHECK(device.adb_port() == 0U);
}
