#include <algorithm>
#include <bit>
#include <cstring>
#include <random>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include <benchmark/benchmark.h>

// ------------------------------------------------------------------------
// Types & Core Logic
// ------------------------------------------------------------------------

struct proxy_item {
  uint64_t         proxy;
  std::string_view key;
  int              value;
};

[[nodiscard]] constexpr auto
pair_comparator(const std::pair<std::string_view, int>& a, const std::pair<std::string_view, int>& b) noexcept -> bool {
  return a.first < b.first;
}

[[nodiscard]] constexpr auto proxy_comparator(const proxy_item& a, const proxy_item& b) noexcept -> bool {
  if (a.proxy != b.proxy) {
    return a.proxy < b.proxy;
  }
  return a.key < b.key;
}

[[nodiscard]] inline auto build_proxy(std::string_view s) noexcept -> uint64_t {
  auto       buffer   = uint64_t{0};
  const auto copy_len = std::min<std::size_t>(s.size(), sizeof(uint64_t));

  if (copy_len > 0) {
    std::memcpy(&buffer, s.data(), copy_len);
  }

  if constexpr (std::endian::native == std::endian::little) {
    return std::byteswap(buffer);
  }
  return buffer;
}

// ------------------------------------------------------------------------
// Data Generation
// ------------------------------------------------------------------------

[[nodiscard]] inline auto generate_strings(std::size_t num_elements, bool high_entropy) -> std::vector<std::string> {
  auto result = std::vector<std::string>{};
  result.reserve(num_elements);
  for (auto i = std::size_t{0}; i < num_elements; ++i) {
    auto suffix = std::to_string(i);
    suffix.insert(suffix.begin(), 5 - std::min(std::size_t{5}, suffix.size()), '0');

    if (high_entropy) {
      result.push_back("id_" + suffix + "_trailing_padding_data");
    } else {
      result.push_back("property_" + suffix + "_padding_data");
    }
  }
  return result;
}

// Pre-allocate the maximum bounds to keep memory stable
static const auto high_entropy_storage = generate_strings(256, true);
static const auto low_entropy_storage  = generate_strings(256, false);

[[nodiscard]] inline auto get_shuffled_pairs(const std::vector<std::string>& storage, std::size_t n)
  -> std::vector<std::pair<std::string_view, int>> {
  auto result = std::vector<std::pair<std::string_view, int>>{};
  result.reserve(n);
  auto i = int{0};

  // C++20/23 ranges to elegantly slice the storage arrays
  for (const auto& s : storage | std::views::take(n)) {
    result.emplace_back(s, i++);
  }

  auto gen = std::mt19937{42};
  std::ranges::shuffle(result, gen);
  return result;
}

[[nodiscard]] inline auto get_shuffled_proxies(const std::vector<std::string>& storage, std::size_t n)
  -> std::vector<proxy_item> {
  auto result = std::vector<proxy_item>{};
  result.reserve(n);
  auto i = int{0};

  for (const auto& s : storage | std::views::take(n)) {
    result.push_back(proxy_item{build_proxy(s), s, i++});
  }

  auto gen = std::mt19937{42};
  std::ranges::shuffle(result, gen);
  return result;
}

// ------------------------------------------------------------------------
// Benchmarks
// ------------------------------------------------------------------------

static void bm_baseline_high_entropy(benchmark::State& state) {
  const auto n      = static_cast<std::size_t>(state.range(0));
  auto       source = get_shuffled_pairs(high_entropy_storage, n);
  auto       work   = std::vector<std::pair<std::string_view, int>>(n);

  for (auto _ : state) {
    std::ranges::copy(source, work.begin());
    benchmark::DoNotOptimize(work.data());

    std::stable_sort(work.begin(), work.end(), pair_comparator);

    benchmark::DoNotOptimize(work.data());
    benchmark::ClobberMemory();
  }
}

BENCHMARK(bm_baseline_high_entropy)->Arg(32)->Arg(256);

static void bm_proxy_high_entropy(benchmark::State& state) {
  const auto n      = static_cast<std::size_t>(state.range(0));
  auto       source = get_shuffled_proxies(high_entropy_storage, n);
  auto       work   = std::vector<proxy_item>(n);

  for (auto _ : state) {
    std::ranges::copy(source, work.begin());
    benchmark::DoNotOptimize(work.data());

    std::stable_sort(work.begin(), work.end(), proxy_comparator);

    benchmark::DoNotOptimize(work.data());
    benchmark::ClobberMemory();
  }
}

BENCHMARK(bm_proxy_high_entropy)->Arg(32)->Arg(256);

static void bm_baseline_low_entropy(benchmark::State& state) {
  const auto n      = static_cast<std::size_t>(state.range(0));
  auto       source = get_shuffled_pairs(low_entropy_storage, n);
  auto       work   = std::vector<std::pair<std::string_view, int>>(n);

  for (auto _ : state) {
    std::ranges::copy(source, work.begin());
    benchmark::DoNotOptimize(work.data());

    std::stable_sort(work.begin(), work.end(), pair_comparator);

    benchmark::DoNotOptimize(work.data());
    benchmark::ClobberMemory();
  }
}

BENCHMARK(bm_baseline_low_entropy)->Arg(32)->Arg(256);

static void bm_proxy_low_entropy(benchmark::State& state) {
  const auto n      = static_cast<std::size_t>(state.range(0));
  auto       source = get_shuffled_proxies(low_entropy_storage, n);
  auto       work   = std::vector<proxy_item>(n);

  for (auto _ : state) {
    std::ranges::copy(source, work.begin());
    benchmark::DoNotOptimize(work.data());

    std::stable_sort(work.begin(), work.end(), proxy_comparator);

    benchmark::DoNotOptimize(work.data());
    benchmark::ClobberMemory();
  }
}

BENCHMARK(bm_proxy_low_entropy)->Arg(32)->Arg(256);
