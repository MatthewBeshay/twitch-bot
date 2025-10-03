/*
Module Name:
- error.hpp

Abstract:
- Defines tb::net error codes and a std::error_category so callers can use
  std::error_code with network helpers. Provides make_error_code and enables
  implicit conversion via is_error_code_enum.
*/
#pragma once

// C++ Standard Library
#include <string>
#include <system_error>

namespace tb::net
{

    enum class errc
    {
        unsupported_encoding = 1,
        decompression_failure,
        invalid_content_type,
    };

    // Category for tb::net errors.
    struct error_category_impl final : std::error_category
    {
        const char* name() const noexcept override
        {
            return "tb.net";
        }
        std::string message(int ev) const override
        {
            switch (static_cast<errc>(ev))
            {
            case errc::unsupported_encoding:
                return "unsupported content-encoding";
            case errc::decompression_failure:
                return "decompression failure";
            case errc::invalid_content_type:
                return "invalid content-type";
            }
            return "unknown tb.net error";
        }
    };

    inline const std::error_category& error_category()
    {
        static error_category_impl cat;
        return cat;
    }

    inline std::error_code make_error_code(errc e) noexcept
    {
        return { static_cast<int>(e), error_category() };
    }

} // namespace tb::net

// Enable implicit conversion to std::error_code for tb::net::errc.
namespace std
{
    template<>
    struct is_error_code_enum<tb::net::errc> : true_type
    {
    };
} // namespace std
