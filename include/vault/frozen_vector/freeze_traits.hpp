#ifndef FROZEN_FREEZE_TRAITS_HPP
#define FROZEN_FREEZE_TRAITS_HPP

#include <memory>
#include <utility>

namespace frozen {

// Primary template
template <typename MutableHandle, typename ConstHandle> struct freeze_traits;

// Specialization 1: shared_ptr<T[]> -> shared_ptr<const T[]>
template <typename T>
struct freeze_traits<std::shared_ptr<T[]>, std::shared_ptr<const T[]>> {
  [[nodiscard]]
  static std::shared_ptr<const T[]> freeze(std::shared_ptr<T[]> &&h) noexcept
  {
    return std::move(h);
  }
};

// Specialization 2: unique_ptr<T[]> -> shared_ptr<const T[]>
template <typename T>
struct freeze_traits<std::unique_ptr<T[]>, std::shared_ptr<const T[]>> {
  [[nodiscard]]
  static std::shared_ptr<const T[]> freeze(std::unique_ptr<T[]> &&h)
  {
    return std::shared_ptr<const T[]>(std::move(h));
  }
};

} // namespace frozen

#endif // FROZEN_FREEZE_TRAITS_HPP
