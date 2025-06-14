#pragma once

// C++ Standard Library
#include <chrono>

/// Simple stopwatch-style timer using std::chrono::steady_clock.
///
/// Example usage:
///   Timer t;
///   // ... do work ...
///   auto ms = t.elapsed_cast<std::chrono::milliseconds>();
///   // ms now holds the number of milliseconds since construction or last reset().
class Timer {
public:
    using clock = std::chrono::steady_clock;

    /// Construct and start the timer immediately.
    Timer() noexcept
        : start_(clock::now())
    {}

    /// Reset the timer to "now."
    void reset() noexcept
    {
        start_ = clock::now();
    }

    /// Return the duration since construction or last reset().
    /// @return A clock::duration representing the elapsed time.
    clock::duration elapsed() const noexcept
    {
        return clock::now() - start_;
    }

    /// Return the elapsed time cast to duration type D (e.g. std::chrono::milliseconds).
    /// @tparam D A std::chrono::duration specialization.
    /// @return The elapsed count in units of D (e.g. count of milliseconds).
    template <typename D>
    auto elapsed_cast() const noexcept
    {
        return std::chrono::duration_cast<D>(elapsed()).count();
    }

private:
    clock::time_point start_;
};
