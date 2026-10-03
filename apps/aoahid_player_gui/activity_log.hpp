// SPDX-License-Identifier: MIT
#pragma once

#include "aoahid_player/events.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace gui {

// The GUI's EventSink: keeps the most recent messages for the activity panel
// and wakes the UI thread whenever one arrives from a worker.
class ActivityLog final : public aoap::EventSink {
  public:
    struct Entry {
        aoap::Severity severity;
        std::string time; // "HH:MM:SS"
        std::string text;
    };

    explicit ActivityLog(std::function<void()> wake) : wake_(std::move(wake)) {}

    void message(aoap::Severity severity, std::string_view text) override;

    // UI thread. `visit` runs under the log's lock.
    template <typename Visitor>
    void visit(Visitor&& visitor) const {
        const std::lock_guard lock(mutex_);
        for (const Entry& entry : entries_)
            visitor(entry);
    }
    // Visits only what arrived after version `seen`, and returns the version
    // that matches, so the caller can pass it back next time.
    template <typename Visitor>
    uint64_t visit_since(const uint64_t seen, Visitor&& visitor) const {
        const std::lock_guard lock(mutex_);
        const uint64_t fresh = version_ - seen;
        const size_t skip =
            fresh >= entries_.size() ? 0 : entries_.size() - static_cast<size_t>(fresh);
        for (size_t index = skip; index < entries_.size(); ++index)
            visitor(entries_[index]);
        return version_;
    }
    [[nodiscard]] uint64_t version() const;
    // The newest error or warning, for the status line; empty when none.
    [[nodiscard]] Entry latest_problem() const;
    void clear();

  private:
    static constexpr size_t capacity = 400;

    std::function<void()> wake_;
    mutable std::mutex mutex_;
    std::deque<Entry> entries_;
    uint64_t version_{};
};

} // namespace gui
