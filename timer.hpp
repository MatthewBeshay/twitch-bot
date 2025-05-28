// timer.hpp
#pragma once

#include <chrono>

class Timer {
public:
    using clock = std::chrono::high_resolution_clock;

    Timer() : _start(clock::now()) {}

    // restart the timer
    void reset() { _start = clock::now(); }

    // elapsed time in microseconds since construction or last reset
    long long elapsed_us() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now() - _start
        ).count();
    }

    // elapsed time in milliseconds (floating-point)
    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(
            clock::now() - _start
        ).count();
    }

private:
    clock::time_point _start;
};
