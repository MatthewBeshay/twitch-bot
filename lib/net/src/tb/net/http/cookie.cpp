#include <tb/net/http/cookie.hpp>

#include <algorithm>
#include <charconv>
#include <ctime>
#include <sstream>

#if defined(_WIN32)
  #include <time.h>
#endif

namespace tb::net {

namespace {

// trim helpers (ASCII)
inline void ltrim(std::string_view& sv) {
  while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= 0x20) sv.remove_prefix(1);
}
inline void rtrim(std::string_view& sv) {
  while (!sv.empty() && static_cast<unsigned char>(sv.back())  <= 0x20) sv.remove_suffix(1);
}
inline std::string_view trim(std::string_view sv) {
  ltrim(sv); rtrim(sv); return sv;
}

inline std::string to_lower(std::string_view sv) {
  std::string out; out.reserve(sv.size());
  for (unsigned char c : sv) out.push_back(static_cast<char>(std::tolower(c)));
  return out;
}

inline bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// RFC7231 IMF-fixdate: "Wdy, DD Mon YYYY HH:MM:SS GMT"
// (Most servers use this; we keep it simple for portability)
std::optional<std::chrono::system_clock::time_point>
parse_http_date(std::string_view s) {
  std::tm tm{}; tm.tm_isdst = -1;

  // make a real string for get_time
  std::string str{s};
  std::istringstream is{str};
  is.imbue(std::locale::classic());
  // Try IMF-fixdate first
  is >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S %Z");
  if (!is.fail()) {
    // Force as GMT/UTC regardless of %Z portability
#if defined(_WIN32)
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    if (t != static_cast<time_t>(-1)) {
      return std::chrono::system_clock::from_time_t(t);
    }
  }

  // Fallbacks are possible, but we keep it conservative to avoid false parses
  return std::nullopt;
}

} // namespace

std::optional<Cookie> parse_set_cookie(std::string_view line,
                                       std::string_view default_domain,
                                       std::string_view default_path,
                                       bool /*from_https*/)
{
  if (line.empty()) return std::nullopt;

  // Split on ';'
  std::vector<std::string_view> parts;
  {
    std::string_view sv = line;
    while (!sv.empty()) {
      auto pos = sv.find(';');
      if (pos == std::string_view::npos) {
        parts.push_back(trim(sv));
        break;
      }
      parts.push_back(trim(sv.substr(0, pos)));
      sv.remove_prefix(pos + 1);
    }
  }
  if (parts.empty()) return std::nullopt;

  // name=value
  auto nv = parts[0];
  auto eq = nv.find('=');
  if (eq == std::string_view::npos) return std::nullopt;

  std::string name{trim(nv.substr(0, eq))};
  std::string value{trim(nv.substr(eq + 1))};
  if (name.empty()) return std::nullopt;

  Cookie c{name, value};

  // Defaults (applied if missing)
  c.domain = to_lower(default_domain);
  if (!c.domain.empty() && c.domain.front() == '.') c.domain.erase(0, 1);
  c.path = default_path.empty() ? "/" : std::string{default_path};

  // Attributes
  for (size_t i = 1; i < parts.size(); ++i) {
    auto attr = parts[i];
    if (attr.empty()) continue;

    auto eqpos = attr.find('=');
    std::string_view k = trim(eqpos == std::string_view::npos ? attr
                                                               : attr.substr(0, eqpos));
    std::string_view v = eqpos == std::string_view::npos ? std::string_view{}
                                                         : trim(attr.substr(eqpos + 1));

    if (ieq(k, "expires")) {
      if (auto tp = parse_http_date(v)) c.expires = *tp;
    } else if (ieq(k, "max-age")) {
      int secs = 0;
      auto first = v.data(), last = v.data() + v.size();
      if (std::from_chars(first, last, secs).ec == std::errc{}) {
        c.max_age = secs;
      }
    } else if (ieq(k, "domain")) {
      auto d = to_lower(v);
      // Strip leading dot
      if (!d.empty() && d.front() == '.') d.erase(0, 1);
      c.domain = std::move(d);
    } else if (ieq(k, "path")) {
      c.path = std::string{v.empty() ? "/" : v};
    } else if (ieq(k, "secure")) {
      c.secure = true;
    } else if (ieq(k, "httponly")) {
      c.http_only = true;
    } else if (ieq(k, "samesite")) {
      auto lv = to_lower(v);
      if (lv == "lax") c.same_site = SameSite::kLax;
      else if (lv == "strict") c.same_site = SameSite::kStrict;
      else if (lv == "none") { c.same_site = SameSite::kNone; c.secure = true; }
      else c.same_site = SameSite::kNull;
    } else if (ieq(k, "partitioned")) {
      c.partitioned = true;
      c.secure = true;
    }
  }

  return c; // <-- return a value (fixes the optional error)
}

std::string build_cookie_header(std::span<const Cookie> cookies) {
  std::string out;
  // Reserve a rough estimate to avoid reallocs
  size_t est = 0;
  for (auto& c : cookies) est += c.name.size() + 1 + c.value.size() + 2;
  out.reserve(est + (cookies.size() ? cookies.size() - 1 : 0));

  bool first = true;
  for (auto& c : cookies) {
    if (c.name.empty()) continue;
    if (!first) out.append("; ");
    first = false;
    out.append(c.name).append("=").append(c.value);
  }
  return out;
}

} // namespace tb::net
