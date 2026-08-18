//
// Console log relay — producer and consumer for LOG_SHM::logRing.
//
// A process writes its log to its own terminal, which the console operator
// cannot see. Lines are dropped into the ring instead, and whoever owns the
// console socket drains them out as [F0 EE] frames.
//
//   FILE_LOG(...) ─► logRing ─► ConsoleServer ─► TCP ─► console Log tab
//
// The ring outlived the multi-process stack it was built for: here a single
// process (Pilot) both writes and drains it. The lock-free shape is still the
// right one -- one of the writers is the 500 Hz loop.
//
// Producers must not block: one of them is the 500 Hz RT loop, and the console
// is frequently not connected at all. Writing is therefore unconditional and
// overwrites the oldest slot; readers detect what they missed from the sequence
// rather than being waited on.

#ifndef RBQ_COMMON_LOGRELAY_HPP
#define RBQ_COMMON_LOGRELAY_HPP

#include <algorithm>
#include <cstring>

#include "common/Log.hpp"
#include "common/SharedMemory.hpp"

namespace log_relay {

// Installs a Log sink that copies every line into the ring. Call once, before
// the threads start. `src` labels the process in the console view: the terminal
// no longer prints a process name because each binary owns a tab, but the
// console merges all three into one pane and needs to say who spoke.
inline void install(pLOG_SHM shm, const char* src) {
    if (!shm) return;
    Log::setSink([shm, src](TLogLevel level, const std::string& text) {
        auto& ring = shm->logRing;

        // relaxed: this thread is the only writer of the slot it is about to
        // claim, and the release on the sequence below is what publishes it.
        const uint32_t seq  = ring.writeSeq.load(std::memory_order_relaxed);
        auto&          slot = ring.entries[seq % LOG_SHM::LOG_RING::SLOTS];

        const uint16_t n = static_cast<uint16_t>(
            std::min(text.size(), static_cast<size_t>(LOG_SHM::LOG_RING::TEXT_MAX)));
        slot.seq   = seq;
        slot.level = static_cast<uint8_t>(level);
        slot.len   = n;
        // Both clocks taken here, where the line actually happened. See the note
        // on Slot: a reader draining on a timer cannot reconstruct this.
        // clock_gettime is a vDSO call, tens of nanoseconds, which the 500 Hz
        // loop can afford.
        //
        // t_us was the controller's own `ctrl.localTime`, which this repo has
        // no writer for -- it was always 0. A monotonic clock says the same
        // thing (elapsed, immune to wall-clock steps) and is actually filled.
        struct timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        slot.unix_us = static_cast<uint64_t>(ts.tv_sec) * 1000000ull +
                       static_cast<uint64_t>(ts.tv_nsec) / 1000ull;
        struct timespec mono{};
        clock_gettime(CLOCK_MONOTONIC, &mono);
        slot.t_us    = static_cast<uint64_t>(mono.tv_sec) * 1000000ull +
                       static_cast<uint64_t>(mono.tv_nsec) / 1000ull;
        std::memcpy(slot.text, text.data(), n);
        std::snprintf(slot.src, sizeof(slot.src), "%s", src);

        // release: everything written above is visible to a reader that sees
        // this sequence. Without it the reader can observe the bumped counter
        // while the text is still being copied.
        ring.writeSeq.store(seq + 1, std::memory_order_release);
    });
}

// Consumer side. Holds its own position so several readers could coexist.
class Reader {
public:
    explicit Reader(pLOG_SHM shm) : m_shm(shm) {
        // Start as far back as the ring still holds, not at the head. Startup
        // is exactly what an operator opening the console wants to read, and a
        // pane that stays empty until something happens reads as "not
        // connected". At most SLOTS lines, and the console bounds its own view
        // again on top of that.
        if (!m_shm) return;
        const uint32_t head = m_shm->logRing.writeSeq.load(std::memory_order_acquire);
        m_readSeq = head > LOG_SHM::LOG_RING::SLOTS
                        ? head - LOG_SHM::LOG_RING::SLOTS
                        : 0;
    }

    // Calls fn(level, src, text, len, unix_us, t_us) for each line since the last
    // drain, oldest first. The two stamps come from the slot, set when the line
    // was written; nothing here re-derives them. Returns how many lines were lost
    // to overwrite before the ones delivered — non-zero means the reader fell
    // more than SLOTS behind.
    template <typename F>
    uint32_t drain(F&& fn) {
        if (!m_shm) return 0;
        auto&          ring = m_shm->logRing;
        const uint32_t head = ring.writeSeq.load(std::memory_order_acquire);

        uint32_t lost = 0;
        if (head - m_readSeq > LOG_SHM::LOG_RING::SLOTS) {
            lost      = head - m_readSeq - LOG_SHM::LOG_RING::SLOTS;
            m_readSeq = head - LOG_SHM::LOG_RING::SLOTS;
        }

        for (; m_readSeq != head; ++m_readSeq) {
            const auto& slot = ring.entries[m_readSeq % LOG_SHM::LOG_RING::SLOTS];
            // The writer may have lapped us between the check above and here.
            // The slot's own sequence says whether what we are reading is still
            // the line we asked for; if not, skip it rather than emit a torn mix
            // of two messages.
            if (slot.seq != m_readSeq) continue;
            fn(slot.level, slot.src, slot.text,
               static_cast<uint16_t>(std::min<uint16_t>(
                   slot.len, LOG_SHM::LOG_RING::TEXT_MAX)),
               slot.unix_us, slot.t_us);
        }
        return lost;
    }

private:
    pLOG_SHM m_shm     = nullptr;
    uint32_t        m_readSeq = 0;
};

} // namespace log_relay

#endif // RBQ_COMMON_LOGRELAY_HPP
