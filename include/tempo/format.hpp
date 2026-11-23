#pragma once

#include <string>

namespace tempo {

/// Locale-independent formatting helpers.
///
/// The library never relies on the process's global C++ locale for persisted
/// or recorded text. These helpers imbue `std::locale::classic()` explicitly so
/// a host application calling `setlocale`/`std::locale::global` cannot change
/// watch values, JSON numbers or clock display.

/// General format with `precision` significant digits (like printf "%.Ng").
std::string format_general(double value, int precision);

/// Fixed format with `precision` digits after the decimal point.
std::string format_fixed(double value, int precision);

/// JSON number text: "null" for NaN/Infinity, otherwise enough digits to round
/// trip a double. Locale-independent.
std::string format_json_number(double value);

}  // namespace tempo
