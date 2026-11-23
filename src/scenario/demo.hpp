#pragma once

#include "tempo/runtime.hpp"

namespace tempo::scenarios {

Task counter_race(Context ctx);
Task flaky_service(Context ctx);
Task pipeline(Context ctx);

}  // namespace tempo::scenarios
