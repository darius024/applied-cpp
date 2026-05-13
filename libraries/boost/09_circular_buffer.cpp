#include <boost/circular_buffer.hpp>
#include <iostream>
#include <numeric>

// compile: g++ -std=c++17 09_circular_buffer.cpp -o 09_circular_buffer

// ─────────────────────────────────────────────────────────────────────
// boost::circular_buffer: fixed-capacity ring buffer. Header-only.
//
// When full, push_back() overwrites the OLDEST element (front).
// Supports O(1) random access, front/back, and bidirectional iterators.
//
// HPC relevance: sliding-window statistics, telemetry rings, event logs,
// and audio/DSP sample buffers — all need a fixed-memory FIFO that
// never allocates after construction.
// ─────────────────────────────────────────────────────────────────────

// Sliding-window mean: always over the last N samples.
class SlidingMean {
public:
    explicit SlidingMean(std::size_t window) : buf_(window) {}

    void push(double v) { buf_.push_back(v); }

    double mean() const {
        if (buf_.empty()) return 0.0;
        return std::accumulate(buf_.begin(), buf_.end(), 0.0) / buf_.size();
    }

    std::size_t size()     const { return buf_.size();     }
    std::size_t capacity() const { return buf_.capacity(); }

private:
    boost::circular_buffer<double> buf_;
};

int main()
{
    // ── Basic ring overwrite ──────────────────────────────────────────
    boost::circular_buffer<int> cb(4); // capacity = 4

    for (int i = 1; i <= 6; ++i) cb.push_back(i);
    // After 1..6: oldest (1,2) were overwritten → [3, 4, 5, 6]

    std::cout << "ring contents: ";
    for (int x : cb) std::cout << x << " ";
    std::cout << "\n"; // 3 4 5 6

    std::cout << "front: " << cb.front()
              << "  back: " << cb.back() << "\n"; // 3  6
    std::cout << "cb[1]: " << cb[1] << "\n";      // O(1) random access → 4
    std::cout << "full: " << std::boolalpha << cb.full() << "\n"; // true

    // ── Sliding mean ──────────────────────────────────────────────────
    SlidingMean sm(5);
    for (int i = 1; i <= 8; ++i) {
        sm.push(static_cast<double>(i));
        std::cout << "pushed " << i
                  << "  window=" << sm.size() << "/" << sm.capacity()
                  << "  mean=" << sm.mean() << "\n";
    }
    // After i=8: window holds [4,5,6,7,8], mean = 6.0

    // ── circular_buffer_space_optimized ──────────────────────────────
    // Same API but only allocates memory for elements actually present;
    // useful when the buffer is usually sparse but can grow to capacity.
    boost::circular_buffer_space_optimized<int> sopt(8);
    sopt.push_back(10);
    sopt.push_back(20);
    std::cout << "space_optimized size: " << sopt.size()
              << " capacity: " << sopt.capacity() << "\n"; // 2 / 8
}
