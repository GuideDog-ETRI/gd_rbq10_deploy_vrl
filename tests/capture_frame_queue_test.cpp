#include "CaptureFrameQueue.hpp"
#include <cassert>
#include <iostream>

int main() {
    CaptureFrameQueue q;
    CaptureFrameQueue::Batch out;
    constexpr int64_t id = 1000000000LL;
    for (int c = 0; c < 7; ++c) q.push(c, {id, 1000 + c * 12, {uint8_t(c)}});
    assert(!q.take(id + 100000000, 1100, out));
    q.push(7, {id, 1090, {7}});
    // 90 ms receive skew still represents exactly one capture.
    assert(q.take(id + 100000000, 1100, out));
    for (int c = 0; c < 8; ++c) assert(out[c].captureNs == id && out[c].bytes[0] == c);
    assert(!q.take(id + 110000000, 1110, out));
    for (int c = 0; c < 8; ++c) q.push(c, {id + 200000000 + (c == 7), 1200, {1}});
    assert(!q.take(id + 210000000, 1210, out)); // mixed captures rejected
    for (int c = 0; c < 8; ++c) q.push(c, {id + 300000000, 1300, {2}});
    assert(!q.take(id + 299000000, 1310, out)); // future source timestamp
    assert(!q.take(id + 550000000, 1550, out)); // 250 ms source expiry
    for (int c = 0; c < 8; ++c) q.push(c, {id + 600000000, 1600, {3}});
    assert(q.take(id + 610000000, 1610, out)); // fresh recovery
    for (int c = 0; c < 8; ++c) q.push(c, {id + 600000000, 1610, {4}});
    assert(!q.take(id + 620000000, 1620, out)); // duplicate not replayed
    std::cout << "PASS: exact capture matching, skew, incomplete, age, future, recovery, duplicates\n";
}
