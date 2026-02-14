#ifndef VIEW_FACTORY_HPP
#define VIEW_FACTORY_HPP

#include <string>
#include <vector>

#include <vault/map_view/map_view.hpp>

namespace bench {
  // We use a fixed key/value type for the benchmark
  using view_t = lib::map_view<std::string, int>;

  /**
   * @brief Factory that initializes a container and returns a view.
   * * The implementation is hidden in a separate .cpp file.
   */
  [[nodiscard]] auto get_opaque_view(std::size_t count) -> view_t;

  /**
   * @brief Returns the shuffled keys used for the benchmark.
   */
  [[nodiscard]] auto get_keys() -> const std::vector<std::string>&;
} // namespace bench

#endif
