// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace aoap::waits {

// libaoahid takes every wait as an argument of the call that waits and has no
// default for it; these are this application's own values.
// Each synchronous AOA control request (node open and close, device close).
inline constexpr uint32_t control_ms = 500U;
// The deadline of one report transfer.
inline constexpr uint32_t send_ms = 500U;
// What a close waits for reports in flight and for the neutral reports it sends.
inline constexpr uint32_t drain_ms = 1000U;

} // namespace aoap::waits
