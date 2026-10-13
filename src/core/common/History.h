#pragma once
// Fixed-size ring buffer of the last N snapshots, indexed by physics frame.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/common/Snapshot.h"

namespace rlt {

class TickHistory {
public:
    explicit TickHistory(size_t capacity = 600) : buf_(capacity) {}

    void push(const Snapshot& s) {
        buf_[head_] = s;
        head_ = (head_ + 1) % buf_.size();
        if (size_ < buf_.size()) ++size_;
    }
    void clear() { size_ = 0; head_ = 0; }

    size_t size() const { return size_; }
    size_t capacity() const { return buf_.size(); }
    bool empty() const { return size_ == 0; }

    // i = 0 is the newest snapshot, i = size()-1 the oldest.
    const Snapshot& back(size_t i = 0) const { return buf_[(head_ + buf_.size() - 1 - i) % buf_.size()]; }
    const Snapshot& newest() const { return back(0); }

    // Newest snapshot with frame <= f, or nullptr.
    const Snapshot* atOrBefore(int64_t f) const {
        for (size_t i = 0; i < size_; ++i) {
            const Snapshot& s = back(i);
            if (s.frame <= f) return &s;
        }
        return nullptr;
    }
    // Oldest snapshot with frame > f, or nullptr.
    const Snapshot* after(int64_t f) const {
        const Snapshot* best = nullptr;
        for (size_t i = 0; i < size_; ++i) {
            const Snapshot& s = back(i);
            if (s.frame > f) best = &s;
            else break;
        }
        return best;
    }

private:
    std::vector<Snapshot> buf_;
    size_t head_ = 0;
    size_t size_ = 0;
};

} // namespace rlt
