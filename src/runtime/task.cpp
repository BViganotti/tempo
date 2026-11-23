#include "tempo/runtime.hpp"

#include <utility>

namespace tempo {

Task Task::promise_type::get_return_object() noexcept {
  return Task{handle_type::from_promise(*this)};
}

Task& Task::operator=(Task&& other) noexcept {
  if (this != &other) {
    if (handle_) {
      handle_.destroy();
    }
    handle_ = std::exchange(other.handle_, {});
  }
  return *this;
}

Task::~Task() {
  if (handle_) {
    handle_.destroy();
  }
}

}  // namespace tempo
