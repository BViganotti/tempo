#include <limits>
#include <locale>
#include <string>

#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/runtime.hpp"

using namespace tempo;

namespace {

class CommaNumpunct : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '.'; }
  std::string do_grouping() const override { return "\3"; }
};

Task locale_watcher(Context ctx) {
  ctx.watch("value", 1.5);
  co_return;
}

}  // namespace

TEST(watch_double_is_reproducible_under_any_global_locale) {
  const std::locale original = std::locale::global(std::locale::classic());
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("locale", locale_watcher);
  CHECK(scheduler.run().ok);
  CHECK_EQ(host.find(EventType::Watch)->s2, std::string("1.5"));

  std::locale::global(std::locale(std::locale::classic(), new CommaNumpunct));
  test::TestHost comma_host;
  Scheduler comma_scheduler(comma_host);
  comma_scheduler.spawn_root("locale", locale_watcher);
  CHECK(comma_scheduler.run().ok);
  std::locale::global(original);
  CHECK_EQ(comma_host.find(EventType::Watch)->s2, std::string("1.5"));
}
