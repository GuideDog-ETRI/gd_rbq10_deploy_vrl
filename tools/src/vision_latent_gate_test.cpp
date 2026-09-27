#include "VisionLatentGate.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    using State = VisionLatentGate::State;
    VisionLatentGate gate;
    auto check = [](bool ok) { if (!ok) { std::cerr << "FAIL\n"; std::exit(1); } };
    gate.reset(100);
    check(gate.update(false, -1, 100) == State::Waiting);
    check(gate.update(false, -1, 1099) == State::Waiting);
    check(gate.update(false, -1, 1100) == State::Fault);
    check(gate.update(true, 0, 1101) == State::Fault);  // no automatic restart
    gate.reset(2000);
    check(gate.update(true, 0, 2000) == State::Fresh);
    check(gate.update(true, 249, 2249) == State::Fresh);
    check(gate.update(true, 250, 2250) == State::Held);
    check(gate.update(true, 999, 2999) == State::Held);
    check(gate.update(true, 0, 3000) == State::Fresh);  // new image, no zero interposed
    check(gate.update(true, 1000, 4000) == State::Fault);
    check(gate.update(true, 0, 4001) == State::Fault);
    gate.reset(5000);
    check(gate.update(true, 0, 5000) == State::Fresh);
    check(gate.update(false, -1, 5001) == State::Fault);
    gate.reset(6000);
    check(gate.update(true, -1, 6000) == State::Fault);
    gate.reset(7000);
    check(gate.update(false, -1, 6999) == State::Fault);
    std::cout << "PASS: startup, 250/1000ms boundaries, recovery, latched fault, reset\n";
}
