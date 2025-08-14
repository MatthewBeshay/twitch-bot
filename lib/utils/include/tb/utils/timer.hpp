#pragma once

// C++ Standard Library
#include <chrono>
#include <concepts>
#include <type_traits>

// GSL
#include <gsl/gsl>

// D must be a std::chrono::duration (cv/ref-qualified types are accepted)
template <class D>
concept ChronoDuration = requires {
    typename std::remove_cvref_t<D>::rep;
    typename std::remove_cvref_t<D>::period;
} && std::same_as<std::remove_cvref_t<D>, std::chrono::duration<typename std::remove_cvref_t<D>::rep, typename std::remove_cvref_t<D>::period>>;

class Timer
{
public:
    using clock = std::chrono::steady_clock;
    static_assert(clock::is_steady, "Timer requires a steady clock");

    Timer() noexcept = default;

    void reset() noexcept
    {
        start_ = clock::now();
    }

    // Elapsed time since construction or last reset
    [[nodiscard]] auto elapsed() const noexcept -> clock::duration
    {
        const auto d = clock::now() - start_;
        Ensures(d >= clock::duration::zero()); // steady clocks are monotonic
        return d;
    }

    // Elapsed time converted to D; returns the raw count of D
    template <ChronoDuration D>
    [[nodiscard]] auto elapsed_count() const noexcept -> typename std::remove_cvref_t<D>::rep
    {
        using DT = std::remove_cvref_t<D>;
        return std::chrono::duration_cast<DT>(elapsed()).count();
    }

    // Elapsed time converted to D; returns a duration object of type D
    template <ChronoDuration D>
    [[nodiscard]] auto elapsed_duration() const noexcept -> std::remove_cvref_t<D>
    {
        using DT = std::remove_cvref_t<D>;
        return std::chrono::duration_cast<DT>(elapsed());
    }

    // Convenience: get elapsed and reset in one call
    [[nodiscard]] auto elapsed_and_reset() noexcept -> clock::duration
    {
        const auto d = elapsed();
        reset();
        return d;
    }

private:
    clock::time_point start_ = clock::now(); // initialized on construction
};
