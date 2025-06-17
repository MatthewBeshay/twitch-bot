#pragma once

// C++ Standard Library
#include <chrono>

/// Stopwatch using std::chrono::steady_clock.
///
/// Example:
///   Timer t;
///   ... work ...
///   auto ms = t.elapsed_cast<std::chrono::milliseconds>();
///   // ms holds elapsed milliseconds since construction or last reset()
class Timer
{
public:
    using clock = std::chrono::steady_clock;

    /// Start the timer.
    Timer() noexcept : start_(clock::now())
    {
    }

    /// Restart the timer.
    void reset() noexcept
    {
        start_ = clock::now();
    }

    /// Elapsed time since construction or last reset().
    [[nodiscard]] auto elapsed() const noexcept -> clock::duration
    {
        return clock::now() - start_;
    }

    /// Elapsed time cast to duration D (e.g. std::chrono::milliseconds).
    template <typename D> auto elapsed_cast() const noexcept
    {
        return std::chrono::duration_cast<D>(elapsed()).count();
    }

private:
    clock::time_point start_;
};
