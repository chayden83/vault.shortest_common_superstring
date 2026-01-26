#include <algorithm>
#include <bit>
#include <cassert>
#include <compare>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @brief segmented_vector
 * A C++23 container with stable references, stable iterators, and fast random
 * access.
 * * * Optimization Update:
 * Uses "Cursor Optimization" for push_back/emplace_back.
 * Maintains a direct pointer (m_push_cursor) to the next write location
 * to bypass block lookup logic during sequential insertion.
 */
template <
    typename T,
    typename Allocator = std::allocator<T>,
    typename InitialCapacity = std::integral_constant<
        std::size_t,
        (sizeof(T) > 4096) ? 1 : std::bit_floor(4096 / sizeof(T))>>
class segmented_vector {
  static_assert(
      std::has_single_bit(InitialCapacity::value),
      "InitialCapacity must be a power of 2."
  );

  static constexpr std::size_t k_initial_cap = InitialCapacity::value;
  static constexpr int k_initial_shift = std::countr_zero(k_initial_cap);

public:
  using value_type = T;
  using allocator_type = Allocator;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;

  using AllocTraits = std::allocator_traits<Allocator>;
  using pointer = typename AllocTraits::pointer;
  using const_pointer = typename AllocTraits::const_pointer;

  template <bool IsConst> class iterator_impl;
  using iterator = iterator_impl<false>;
  using const_iterator = iterator_impl<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
  using BlockPtr = T *;
  using SpineAllocator = typename AllocTraits::template rebind_alloc<BlockPtr>;
  using Spine = std::vector<BlockPtr, SpineAllocator>;

  [[no_unique_address]] Allocator m_allocator;
  Spine m_spine;
  size_type m_size = 0;
  size_type m_capacity = 0;

  // Cursor Optimization Members
  // Points to the memory location where the NEXT element will be constructed.
  T *m_push_cursor = nullptr;
  // Points to the end of the current block (one past the valid range).
  T *m_push_limit = nullptr;
  // Tracks which block the cursor is currently in.
  size_type m_current_block_idx = 0;

  // -------------------------------------------------------------------------
  // Optimized Indexing Logic
  // -------------------------------------------------------------------------
  [[nodiscard]] [[gnu::always_inline]] inline std::pair<size_type, size_type>
  get_location(size_type index) const noexcept
  {
    const size_type scaled_index = index >> k_initial_shift;
    const size_type k = std::bit_width(scaled_index);

    const size_type safe_scaled = scaled_index | size_type(1);
    const size_type calculated_start = std::bit_floor(safe_scaled)
                                       << k_initial_shift;
    const size_type mask = 0 - static_cast<size_type>(scaled_index != 0);
    const size_type block_start = calculated_start & mask;

    return {k, index ^ block_start};
  }

  [[nodiscard]] constexpr size_type
  get_block_capacity(size_type block_idx) const noexcept
  {
    if (block_idx == 0)
      return k_initial_cap;
    return k_initial_cap << (block_idx - 1);
  }

  // Helper to reset cursors (e.g. after clear or construct)
  void reset_cursors()
  {
    if (m_spine.empty()) {
      m_push_cursor = nullptr;
      m_push_limit = nullptr;
      m_current_block_idx = 0;
    } else {
      // Point to start of Block 0
      m_current_block_idx = 0;
      m_push_cursor = m_spine[0];
      m_push_limit = m_spine[0] + get_block_capacity(0);
    }
  }

  // Helper to sync cursors to a specific size (used after copy/move)
  void sync_cursors_to_size()
  {
    if (m_size == m_capacity) {
      // Full, force slow path on next push
      m_push_cursor = nullptr;
      m_push_limit = nullptr;
    } else {
      // Partially full, locate correct block and offset
      auto [k, off] = get_location(m_size);
      m_current_block_idx = k;
      m_push_cursor = m_spine[k] + off;
      m_push_limit = m_spine[k] + get_block_capacity(k);
    }
  }

public:
  // -------------------------------------------------------------------------
  // Constructors
  // -------------------------------------------------------------------------

  [[nodiscard]] segmented_vector(
  ) noexcept(std::is_nothrow_default_constructible_v<Allocator>)
      : m_allocator()
      , m_spine(m_allocator)
  {}

  [[nodiscard]] explicit segmented_vector(const Allocator &alloc)
      : m_allocator(alloc)
      , m_spine(alloc)
  {}

  [[nodiscard]] segmented_vector(const segmented_vector &other)
      : m_allocator(
            AllocTraits::select_on_container_copy_construction(other.m_allocator
            )
        )
      , m_spine(m_allocator)
  {
    try {
      copy_from(other);
    } catch (...) {
      clear();
      deallocate_all_blocks();
      throw;
    }
  }

  [[nodiscard]] segmented_vector(
      const segmented_vector &other, const Allocator &alloc
  )
      : m_allocator(alloc)
      , m_spine(alloc)
  {
    try {
      copy_from(other);
    } catch (...) {
      clear();
      deallocate_all_blocks();
      throw;
    }
  }

  [[nodiscard]] segmented_vector(segmented_vector &&other) noexcept
      : m_allocator(std::move(other.m_allocator))
      , m_spine(std::move(other.m_spine), m_allocator)
      , m_size(other.m_size)
      , m_capacity(other.m_capacity)
      , m_push_cursor(other.m_push_cursor)
      , m_push_limit(other.m_push_limit)
      , m_current_block_idx(other.m_current_block_idx)
  {
    other.m_size = 0;
    other.m_capacity = 0;
    other.m_push_cursor = nullptr;
    other.m_push_limit = nullptr;
  }

  [[nodiscard]] segmented_vector(
      segmented_vector &&other, const Allocator &alloc
  )
      : m_allocator(alloc)
      , m_spine(alloc)
  {
    if (alloc == other.m_allocator) {
      m_spine = std::move(other.m_spine);
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      m_push_cursor = other.m_push_cursor;
      m_push_limit = other.m_push_limit;
      m_current_block_idx = other.m_current_block_idx;

      other.m_size = 0;
      other.m_capacity = 0;
      other.m_push_cursor = nullptr;
      other.m_push_limit = nullptr;
    } else {
      try {
        copy_from(other); // Actually move-constructs via emplace logic below if
                          // implemented tailored
        // For simplicity, reusing copy_from logic but with moves requires
        // manual loop Since this is rare, we rely on standard construction.
      } catch (...) {
        clear();
        deallocate_all_blocks();
        throw;
      }
    }
  }

  ~segmented_vector()
  {
    clear();
    deallocate_all_blocks();
  }

  // -------------------------------------------------------------------------
  // Assignment
  // -------------------------------------------------------------------------
  segmented_vector &operator=(const segmented_vector &other)
  {
    if (this == &other)
      return *this;
    if constexpr (AllocTraits::propagate_on_container_copy_assignment::value) {
      if (m_allocator != other.m_allocator) {
        clear();
        deallocate_all_blocks();
        m_allocator = other.m_allocator;
        m_spine = Spine(m_allocator);
        m_capacity = 0;
      }
    }
    segmented_vector temp(other, m_allocator);
    swap(temp);
    return *this;
  }

  segmented_vector &operator=(segmented_vector &&other) noexcept(
      AllocTraits::propagate_on_container_move_assignment::value ||
      AllocTraits::is_always_equal::value
  )
  {
    if (this == &other)
      return *this;

    if constexpr (AllocTraits::propagate_on_container_move_assignment::value) {
      clear();
      deallocate_all_blocks();
      m_allocator = std::move(other.m_allocator);
      m_spine = std::move(other.m_spine);
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      m_push_cursor = other.m_push_cursor;
      m_push_limit = other.m_push_limit;
      m_current_block_idx = other.m_current_block_idx;
    } else if (m_allocator == other.m_allocator) {
      clear();
      deallocate_all_blocks();
      m_spine = std::move(other.m_spine);
      m_size = other.m_size;
      m_capacity = other.m_capacity;
      m_push_cursor = other.m_push_cursor;
      m_push_limit = other.m_push_limit;
      m_current_block_idx = other.m_current_block_idx;
    } else {
      clear();
      for (auto &elem : other)
        emplace_back(std::move(elem));
    }

    other.m_size = 0;
    other.m_capacity = 0;
    other.m_spine.clear();
    other.m_push_cursor = nullptr;
    other.m_push_limit = nullptr;
    return *this;
  }

  // -------------------------------------------------------------------------
  // Access
  // -------------------------------------------------------------------------
  [[nodiscard]] reference operator[](size_type index) noexcept
  {
    auto [k, off] = get_location(index);
    return *(m_spine[k] + off);
  }

  [[nodiscard]] const_reference operator[](size_type index) const noexcept
  {
    auto [k, off] = get_location(index);
    return *(m_spine[k] + off);
  }

  [[nodiscard]] reference at(size_type index)
  {
    if (index >= m_size)
      throw std::out_of_range("segmented_vector::at");
    return (*this)[index];
  }

  [[nodiscard]] const_reference at(size_type index) const
  {
    if (index >= m_size)
      throw std::out_of_range("segmented_vector::at");
    return (*this)[index];
  }

  [[nodiscard]] reference front() noexcept
  {
    return (*this)[0];
  }
  [[nodiscard]] const_reference front() const noexcept
  {
    return (*this)[0];
  }
  [[nodiscard]] reference back() noexcept
  {
    return (*this)[m_size - 1];
  }
  [[nodiscard]] const_reference back() const noexcept
  {
    return (*this)[m_size - 1];
  }

  // -------------------------------------------------------------------------
  // Capacity & Modifiers
  // -------------------------------------------------------------------------
  [[nodiscard]] bool empty() const noexcept
  {
    return m_size == 0;
  }
  [[nodiscard]] size_type size() const noexcept
  {
    return m_size;
  }
  [[nodiscard]] size_type max_size() const noexcept
  {
    return AllocTraits::max_size(m_allocator);
  }
  [[nodiscard]] size_type capacity() const noexcept
  {
    return m_capacity;
  }

  void clear() noexcept
  {
    for (size_type i = m_size; i > 0; --i) {
      auto [k, off] = get_location(i - 1);
      AllocTraits::destroy(m_allocator, m_spine[k] + off);
    }
    m_size = 0;
    // Capacity preserved. Reset cursors to start of Block 0.
    reset_cursors();
  }

  // -------------------------------------------------------------------------
  // Optimized Push Back / Emplace Back
  // -------------------------------------------------------------------------
  void push_back(const T &value)
  {
    emplace_back(value);
  }
  void push_back(T &&value)
  {
    emplace_back(std::move(value));
  }

  template <typename... Args> reference emplace_back(Args &&...args)
  {
    // FAST PATH: Cursor is valid and within bounds
    if (m_push_cursor != m_push_limit) [[likely]] {
      AllocTraits::construct(
          m_allocator, m_push_cursor, std::forward<Args>(args)...
      );
      reference ret = *m_push_cursor;
      ++m_push_cursor;
      ++m_size;
      return ret;
    }

    // SLOW PATH: Boundary crossing or allocation needed
    return emplace_back_slow(std::forward<Args>(args)...);
  }

private:
  // Noinline to keep the hot path instruction cache usage minimal
  template <typename... Args>
  [[gnu::noinline]] reference emplace_back_slow(Args &&...args)
  {
    // If m_size < m_capacity, it means we have allocated blocks ahead.
    // We just need to hop to the next block.
    if (m_size < m_capacity) {
      // Move to next block
      ++m_current_block_idx;
      m_push_cursor = m_spine[m_current_block_idx];
      m_push_limit = m_push_cursor + get_block_capacity(m_current_block_idx);
    } else {
      // No space left, allocate new block
      grow();
      // grow() updates cursor/limit
    }

    // Now safe to construct
    AllocTraits::construct(
        m_allocator, m_push_cursor, std::forward<Args>(args)...
    );
    reference ret = *m_push_cursor;
    ++m_push_cursor;
    ++m_size;
    return ret;
  }

  void grow()
  {
    size_type next_idx = m_spine.size();
    size_type next_size = get_block_capacity(next_idx);
    BlockPtr new_block = AllocTraits::allocate(m_allocator, next_size);

    try {
      m_spine.push_back(new_block);
    } catch (...) {
      AllocTraits::deallocate(m_allocator, new_block, next_size);
      throw;
    }
    m_capacity += next_size;

    // Update cursors to point to the new block
    m_current_block_idx = next_idx;
    m_push_cursor = new_block;
    m_push_limit = new_block + next_size;
  }

  void deallocate_all_blocks()
  {
    for (size_type i = 0; i < m_spine.size(); ++i) {
      AllocTraits::deallocate(m_allocator, m_spine[i], get_block_capacity(i));
    }
    m_spine.clear();
    m_capacity = 0;
    m_push_cursor = nullptr;
    m_push_limit = nullptr;
  }

  void copy_from(const segmented_vector &other)
  {
    // Pre-allocate blocks if needed
    while (m_capacity < other.m_size)
      grow();

    // Reset to start for writing (in case this was called on non-empty, though
    // typically called on fresh) Note: For copy ctor, we are empty. For
    // assignment, we cleared. If we are fresh, cursors might be null if grow()
    // wasn't called (size 0).
    if (m_size == 0 && m_capacity > 0)
      reset_cursors();

    // Copy elements
    // We can use standard emplace_back, it will use the cursors we just set
    // up/grew.
    for (const auto &item : other) {
      emplace_back(item);
    }
  }

public:
  // -------------------------------------------------------------------------
  // Iterator Factories
  // -------------------------------------------------------------------------
  [[nodiscard]] iterator begin() noexcept
  {
    return iterator(this, 0);
  }
  [[nodiscard]] const_iterator begin() const noexcept
  {
    return const_iterator(this, 0);
  }
  [[nodiscard]] const_iterator cbegin() const noexcept
  {
    return const_iterator(this, 0);
  }

  [[nodiscard]] iterator end() noexcept
  {
    return iterator(this, m_size);
  }
  [[nodiscard]] const_iterator end() const noexcept
  {
    return const_iterator(this, m_size);
  }
  [[nodiscard]] const_iterator cend() const noexcept
  {
    return const_iterator(this, m_size);
  }

  [[nodiscard]] reverse_iterator rbegin() noexcept
  {
    return reverse_iterator(end());
  }
  [[nodiscard]] const_reverse_iterator rbegin() const noexcept
  {
    return const_reverse_iterator(end());
  }
  [[nodiscard]] const_reverse_iterator crbegin() const noexcept
  {
    return const_reverse_iterator(end());
  }

  [[nodiscard]] reverse_iterator rend() noexcept
  {
    return reverse_iterator(begin());
  }
  [[nodiscard]] const_reverse_iterator rend() const noexcept
  {
    return const_reverse_iterator(begin());
  }
  [[nodiscard]] const_reverse_iterator crend() const noexcept
  {
    return const_reverse_iterator(begin());
  }

  [[nodiscard]] allocator_type get_allocator() const noexcept
  {
    return m_allocator;
  }

  void swap(segmented_vector &other) noexcept(
      AllocTraits::propagate_on_container_swap::value ||
      AllocTraits::is_always_equal::value
  )
  {
    using std::swap;
    if constexpr (AllocTraits::propagate_on_container_swap::value) {
      swap(m_allocator, other.m_allocator);
    }
    swap(m_spine, other.m_spine);
    swap(m_size, other.m_size);
    swap(m_capacity, other.m_capacity);
    swap(m_push_cursor, other.m_push_cursor);
    swap(m_push_limit, other.m_push_limit);
    swap(m_current_block_idx, other.m_current_block_idx);
  }

  // -------------------------------------------------------------------------
  // Iterator Implementation (Cached)
  // -------------------------------------------------------------------------
  template <bool IsConst> class iterator_impl {
    friend class segmented_vector;
    using ContainerPtr = std::
        conditional_t<IsConst, const segmented_vector *, segmented_vector *>;

    ContainerPtr m_cont;
    size_type m_global_index;
    T *m_current_ptr = nullptr;
    T *m_block_end_ptr = nullptr;

    void update_cache()
    {
      if (!m_cont || m_global_index >= m_cont->m_size) {
        m_current_ptr = nullptr;
        m_block_end_ptr = nullptr;
        return;
      }
      auto [k, off] = m_cont->get_location(m_global_index);
      T *block_start = m_cont->m_spine[k];
      m_current_ptr = block_start + off;
      m_block_end_ptr = block_start + m_cont->get_block_capacity(k);
    }

    iterator_impl(ContainerPtr cont, size_type idx) noexcept
        : m_cont(cont)
        , m_global_index(idx)
    {
      update_cache();
    }

  public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const T *, T *>;
    using reference = std::conditional_t<IsConst, const T &, T &>;

    [[nodiscard]] iterator_impl() noexcept
        : m_cont(nullptr)
        , m_global_index(0)
    {}

    template <
        bool OtherConst,
        typename = std::enable_if_t<IsConst && !OtherConst>>
    [[nodiscard]] iterator_impl(const iterator_impl<OtherConst> &other) noexcept
        : m_cont(other.m_cont)
        , m_global_index(other.m_global_index)
    {
      update_cache();
    }

    [[nodiscard]] reference operator*() const
    {
      return *m_current_ptr;
    }
    [[nodiscard]] pointer operator->() const
    {
      return m_current_ptr;
    }
    [[nodiscard]] reference operator[](difference_type n) const
    {
      return (*m_cont)[m_global_index + n];
    }

    iterator_impl &operator++()
    {
      ++m_global_index;
      ++m_current_ptr;
      if (m_current_ptr == m_block_end_ptr) [[unlikely]] {
        update_cache();
      }
      return *this;
    }
    iterator_impl operator++(int)
    {
      auto tmp = *this;
      ++(*this);
      return tmp;
    }

    iterator_impl &operator--()
    {
      if (m_global_index == 0)
        return *this;
      --m_global_index;
      update_cache();
      return *this;
    }
    iterator_impl operator--(int)
    {
      auto tmp = *this;
      --(*this);
      return tmp;
    }

    iterator_impl &operator+=(difference_type n)
    {
      m_global_index += n;
      update_cache();
      return *this;
    }
    iterator_impl &operator-=(difference_type n)
    {
      m_global_index -= n;
      update_cache();
      return *this;
    }

    [[nodiscard]] friend iterator_impl
    operator+(iterator_impl it, difference_type n)
    {
      return it += n;
    }
    [[nodiscard]] friend iterator_impl
    operator+(difference_type n, iterator_impl it)
    {
      return it += n;
    }
    [[nodiscard]] friend iterator_impl
    operator-(iterator_impl it, difference_type n)
    {
      return it -= n;
    }
    [[nodiscard]] friend difference_type
    operator-(const iterator_impl &lhs, const iterator_impl &rhs)
    {
      return static_cast<difference_type>(lhs.m_global_index) -
             static_cast<difference_type>(rhs.m_global_index);
    }
    [[nodiscard]] friend bool
    operator==(const iterator_impl &lhs, const iterator_impl &rhs)
    {
      return lhs.m_global_index == rhs.m_global_index;
    }
    [[nodiscard]] friend std::strong_ordering
    operator<=>(const iterator_impl &lhs, const iterator_impl &rhs)
    {
      return lhs.m_global_index <=> rhs.m_global_index;
    }
  };
};

template <typename T, typename Alloc, typename IC>
void swap(
    segmented_vector<T, Alloc, IC> &lhs, segmented_vector<T, Alloc, IC> &rhs
) noexcept(noexcept(lhs.swap(rhs)))
{
  lhs.swap(rhs);
}
