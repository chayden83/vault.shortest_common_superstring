#ifndef VAULT_STATIC_INDEX_STATIC_INDEX_HPP
#define VAULT_STATIC_INDEX_STATIC_INDEX_HPP

#include <concepts>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include <function2/function2.hpp>

#include <vault/frozen_vector/frozen_vector.hpp>
#include <vault/frozen_vector/frozen_vector_builder.hpp>

#include <vault/static_index/traits.hpp>

namespace vault::containers {

  struct key_128 {
    uint64_t low;
    uint64_t high;

    bool operator==(const key_128& other) const = default;
  };

  struct key_128_high {
    [[nodiscard]] static constexpr inline uint64_t operator()(auto const &, key_128 const& key) noexcept {
      return key.high;
    }
  };

  template <typename Fingerprint, typename Proj, typename Comp>
  class static_index_builder;

  struct static_index_base {
    static constexpr inline auto npos = std::numeric_limits<std::size_t>::max();

    [[nodiscard]] static_index_base() = default;

    [[nodiscard]] static_index_base(const static_index_base&)     = default;
    [[nodiscard]] static_index_base(static_index_base&&) noexcept = default;

    static_index_base& operator=(const static_index_base&)     = default;
    static_index_base& operator=(static_index_base&&) noexcept = default;

    ~static_index_base();

    // --- Generalized Lookup ---

    using bytes_sequence_visitor_t = fu2::function_view<void(std::span<std::byte const>)>;
    using bytes_sequence_channel_t = fu2::function_view<void(bytes_sequence_visitor_t)>;

    [[nodiscard]] std::pair<std::size_t, key_128> operator[](key_128) const;
    [[nodiscard]] std::pair<std::size_t, key_128> operator[](bytes_sequence_channel_t) const;

    [[nodiscard]] bool   empty() const noexcept;
    [[nodiscard]] size_t memory_usage_bytes() const noexcept;

    [[nodiscard]] static key_128           hash(bytes_sequence_channel_t);
    [[nodiscard]] static static_index_base build(std::span<key_128 const>);

  private:
    struct impl;
    std::shared_ptr<const impl> pimpl_;

    [[nodiscard]] static_index_base(std::shared_ptr<const impl> ptr);
  };

  template <typename Fingerprint = std::size_t, typename Proj = key_128_high, typename Comp = std::equal_to<>>
  class static_index : private static_index_base {
    frozen::frozen_vector<Fingerprint> fingerprints_;

    [[no_unique_address]] Comp comp_;
    [[no_unique_address]] Proj proj_;

    [[nodiscard]] static_index(
      static_index_base                  base,
      frozen::frozen_vector<Fingerprint> fingerprints,
      Proj                               proj,
      Comp                               comp
    )
      : static_index_base(std::move(base))
      , fingerprints_(std::move(fingerprints))
      , comp_(std::move(comp))
      , proj_(std::move(proj)) {}

    friend class static_index_builder<Fingerprint, Proj, Comp>;

  public:
    using static_index_base::npos;
    using static_index_base::empty;
    using static_index_base::memory_usage_bytes;
    
    [[nodiscard]] bool empty() const noexcept {
      return std::ranges::empty(fingerprints_);
    }

    template <concepts::underlying_byte_sequences K>
      requires std::predicate<
        Comp,
        std::invoke_result_t<Proj, K const&, key_128 const&>,
        std::invoke_result_t<Proj, K const&, key_128 const&>>
    [[nodiscard]] std::optional<std::size_t> operator[](K&& item) const noexcept {
      auto byte_sequence_channel = [&](concepts::byte_sequence_visitor auto visitor) {
        traits::underlying_byte_sequences<std::remove_cvref_t<K>>::visit(std::forward<K>(item), visitor);
      };

      auto [slot, hash] = static_index_base::operator[](byte_sequence_channel);

      if (std::invoke(comp_, std::invoke(proj_, item, hash), fingerprints_[slot])) {
        return slot;
      }

      return std::nullopt;
    }
  };

  template <typename Fingerprint = std::size_t, typename Proj = key_128_high, typename Comp = std::equal_to<>>
  class static_index_builder {
    std::vector<key_128>     hashes_;
    std::vector<Fingerprint> fingerprints_;

    [[no_unique_address]] Comp comp_;
    [[no_unique_address]] Proj proj_;

  public:
    [[nodiscard]] static_index_builder() = default;

    [[nodiscard]] explicit static_index_builder(Proj proj)
      : proj_(std::move(proj)) {}

    [[nodiscard]] static_index_builder(Proj proj, Comp comp)
      : comp_(std::move(comp))
      , proj_(std::move(proj)) {}

    template <typename Self, std::ranges::input_range R>
      requires concepts::underlying_byte_sequences<std::remove_cvref_t<std::ranges::range_reference_t<R>>>
    Self add_n(this Self&& self, R&& items) {
      for (auto&& item : items) {
        self.add_1(std::forward<decltype(item)>(item));
      }

      return std::forward<Self>(self);
    }

    template <typename Self, typename T>
      requires concepts::underlying_byte_sequences<std::remove_cvref_t<T>>
    Self add_1(this Self&& self, T&& item) {
      auto hash = static_index_base::hash([&](concepts::byte_sequence_visitor auto visitor) {
        traits::underlying_byte_sequences<std::remove_cvref_t<T>>::visit(item, visitor);
      });

      self.fingerprints_.emplace_back(std::invoke(self.proj_, std::forward<T>(item), hash));

      try {
        self.hashes_.emplace_back(hash);
      } catch (...) {
        self.fingerprints_.pop_back();
        throw;
      }

      return std::forward<Self>(self);
    }

    [[nodiscard]] static_index<Fingerprint, Proj, Comp> build() && {
      auto base = static_index_base::build(hashes_);

      // Permute the fingerprints according to the perfect
      // hash. Otherwise they will not align with the indexes returned
      // when we perofrm a lookup.
      auto permuted_fingerprints = frozen::frozen_vector_builder<Fingerprint>(fingerprints_.size(), Fingerprint{});

      for (auto const& [index, hash] : std::views::enumerate(hashes_)) {
        permuted_fingerprints[index] = std::move(fingerprints_[base[hash].first]);
      }

      return {std::move(base), std::move(permuted_fingerprints).freeze(), std::move(proj_), std::move(comp_)};
    }

    template <std::invocable<std::size_t> Sink>
    [[nodiscard]] std::pair<static_index<Fingerprint, Proj, Comp>, Sink> build(Sink sink) && {
      auto self = std::move(*this).build();

      for (auto const& hash : hashes_) {
        std::invoke(sink, static_cast<static_index_base const &>(self)[hash].first);
      }

      return {std::move(self), std::move(sink)};
    }

    template <std::output_iterator<std::size_t> O>
    [[nodiscard]] std::pair<static_index<Fingerprint, Proj, Comp>, O> build(O out) && {
      auto [self, _] = std::move(*this).build([&](std::size_t target) { *out++ = target; });
      return {std::move(self), std::move(out)};
    }
  };

} // namespace vault::containers

#endif // VAULT_STATIC_INDEX_STATIC_INDEX_HPP
