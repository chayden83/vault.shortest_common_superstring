#include "benchmarks.map_view.hpp"
#include <algorithm>
#include <boost/unordered/unordered_flat_map.hpp>
#include <random>

namespace bench {

  // Static storage to ensure data persistence during benchmarks
  static auto g_container = boost::unordered_flat_map<std::string, int>{};
  static auto g_keys      = std::vector<std::string>{};

  auto get_opaque_view(std::size_t count) -> view_t
  {
    g_container.clear();
    g_keys.clear();
    g_container.reserve(count);

    std::mt19937 gen(42);
    for (std::size_t i = 0; i < count; ++i) {
      auto key         = "key_" + std::to_string(i);
      g_container[key] = static_cast<int>(i);
      g_keys.push_back(key);
    }

    // Shuffle to prevent predictable hardware prefetching patterns
    std::shuffle(g_keys.begin(), g_keys.end(), gen);

    return view_t{g_container};
  }

  auto get_keys() -> const std::vector<std::string>& { return g_keys; }

} // namespace bench
