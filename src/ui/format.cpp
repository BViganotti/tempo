#include "tempo/format.hpp"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace tempo {
namespace {

template <typename Manip>
std::string render(double value, Manip manip) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << manip << value;
  return stream.str();
}

}  // namespace

std::string format_general(double value, int precision) {
  if (precision < 1) {
    precision = 1;
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(precision) << value;
  return stream.str();
}

std::string format_fixed(double value, int precision) {
  if (precision < 0) {
    precision = 0;
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}

std::string format_json_number(double value) {
  if (!std::isfinite(value)) {
    return "null";
  }
  if (value == 0.0) {
    return "0";
  }
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17) << value;
  return stream.str();
}

}  // namespace tempo
