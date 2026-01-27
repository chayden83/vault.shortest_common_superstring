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
 * * * Final Optimization: Implied Size
 * The 'm_size' member has been removed to reduce 'push_back' instruction count.
 * Size is now calculated on-demand using the distance between 'm_push_cursor'
 * and the start of the current block.
 */
template <
    typename T,
    typename Allocator       = std::allocator<T>,
    typename InitialCapacity = std::integral_constant<
        std::size_t,
        (sizeof(T) > 4096) ? 1 : std::bit_floor(4096 / sizeof(T))>>
class segmented_vector {
  static_assert(
      std::has_single_bit(InitialCapacity::value),
      "InitialCapacity must be a power of 2."
  );

  static constexpr std::size_t k_initial_cap = InitialCapacity::value;
  static constexpr int k_initial_shift       = std::countr_zero(k_initial_cap);

public:
  using value_type      = T;
  using allocator_type  = Allocator;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference       = value_type&;
  using const_reference = const value_type&;

  using AllocTraits   = std::allocator_traits<Allocator>;
  using pointer       = typename AllocTraits::pointer;
  using const_pointer = typename AllocTraits::const_pointer;

  template <bool IsConst> class iterator_impl;
  using iterator               = iterator_impl<false>;
  using const_iterator         = iterator_impl<true>;
  using reverse_iterator       = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
  using BlockPtr       = T*;
  using SpineAllocator = typename AllocTraits::template rebind_alloc<BlockPtr>;
  using Spine          = std::vector<BlockPtr, SpineAllocator>;

  [[no_unique_address]] Allocator m_allocator;
  Spine                           m_spine;

  // Implied Size Tracking
  // m_size is removed. We track how many elements are in blocks BEFORE the
  // current one.
  size_type m_size_prefix = 0;
  size_type m_capacity    = 0;

  // Cursor Optimization
  T* m_push_cursor              = nullptr;
  T* m_push_limit               = nullptr;
  T* m_current_block_begin      = nullptr; // Needed to calc size() from cursor
  size_type m_current_block_idx = 0;

  // -------------------------------------------------------------------------
  // Optimized Indexing Logic
  // -------------------------------------------------------------------------
  [[nodiscard]] [[gnu::always_inline]] inline std::pair<size_type, size_type>
  get_location(size_type index) const noexcept
  {
    const size_type scaled_index = index >> k_initial_shift;
    const size_type k            = std::bit_width(scaled_index);

    const size_type safe_scaled      = scaled_index | size_type(1);
    const size_type calculated_start = std::bit_floor(safe_scaled)
                                       << k_initial_shift;
    const size_type mask        = 0 - static_cast<size_type>(scaled_index != 0);
    const size_type block_start = calculated_start & mask;

    return {k, index ^ block_start};
  }

  [[nodiscard]] constexpr size_type
  get_block_capacity(size_type block_idx) const noexcept
  {
    if (block_idx == 0) {
      return k_initial_cap;
    }
    return k_initial_cap << (block_idx - 1);
  }

  void reset_cursors()
  {
    m_size_prefix       = 0;
    m_current_block_idx = 0;
    if (m_spine.empty()) {
      m_push_cursor         = nullptr;
      m_push_limit          = nullptr;
      m_current_block_begin = nullptr;
    } else {
      m_push_cursor         = m_spine[0];
      m_current_block_begin = m_spine[0];
      m_push_limit          = m_spine[0] + get_block_capacity(0);
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

  [[nodiscard]] explicit segmented_vector(const Allocator& alloc)
      : m_allocator(alloc)
      , m_spine(alloc)
  {}

  [[nodiscard]] segmented_vector(const segmented_vector& other)
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
      const segmented_vector& other, const Allocator& alloc
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

  [[nodiscard]] segmented_vector(segmented_vector&& other) noexcept
      : m_allocator(std::move(other.m_allocator))
      , m_spine(std::move(other.m_spine), m_allocator)
      , m_size_prefix(other.m_size_prefix)
      , m_capacity(other.m_capacity)
      , m_push_cursor(other.m_push_cursor)
      , m_push_limit(other.m_push_limit)
      , m_current_block_begin(other.m_current_block_begin)
      , m_current_block_idx(other.m_current_block_idx)
  {
    other.m_size_prefix         = 0;
    other.m_capacity            = 0;
    other.m_push_cursor         = nullptr;
    other.m_push_limit          = nullptr;
    other.m_current_block_begin = nullptr;
  }

  [[nodiscard]] segmented_vector(
      segmented_vector&& other, const Allocator& alloc
  )
      : m_allocator(alloc)
      , m_spine(alloc)
  {
    if (alloc == other.m_allocator) {
      m_spine               = std::move(other.m_spine);
      m_size_prefix         = other.m_size_prefix;
      m_capacity            = other.m_capacity;
      m_push_cursor         = other.m_push_cursor;
      m_push_limit          = other.m_push_limit;
      m_current_block_begin = other.m_current_block_begin;
      m_current_block_idx   = other.m_current_block_idx;

      other.m_size_prefix         = 0;
      other.m_capacity            = 0;
      other.m_push_cursor         = nullptr;
      other.m_push_limit          = nullptr;
      other.m_current_block_begin = nullptr;
    } else {
      try {
        copy_from(other);
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
  segmented_vector& operator=(const segmented_vector& other)
  {
    if (this == &other) {
      return *this;
    }
    if constexpr (AllocTraits::propagate_on_container_copy_assignment::value) {
      if (m_allocator != other.m_allocator) {
        clear();
        deallocate_all_blocks();
        m_allocator = other.m_allocator;
        m_spine     = Spine(m_allocator);
        m_capacity  = 0;
      }
    }
    segmented_vector temp(other, m_allocator);
    swap(temp);
    return *this;
  }

  segmented_vector& operator=(segmented_vector&& other) noexcept(
      AllocTraits::propagate_on_container_move_assignment::value ||
      AllocTraits::is_always_equal::value
  )
  {
    if (this == &other) {
      return *this;
    }

    if constexpr (AllocTraits::propagate_on_container_move_assignment::value) {
      clear();
      deallocate_all_blocks();
      m_allocator           = std::move(other.m_allocator);
      m_spine               = std::move(other.m_spine);
      m_size_prefix         = other.m_size_prefix;
      m_capacity            = other.m_capacity;
      m_push_cursor         = other.m_push_cursor;
      m_push_limit          = other.m_push_limit;
      m_current_block_begin = other.m_current_block_begin;
      m_current_block_idx   = other.m_current_block_idx;
    } else if (m_allocator == other.m_allocator) {
      clear();
      deallocate_all_blocks();
      m_spine               = std::move(other.m_spine);
      m_size_prefix         = other.m_size_prefix;
      m_capacity            = other.m_capacity;
      m_push_cursor         = other.m_push_cursor;
      m_push_limit          = other.m_push_limit;
      m_current_block_begin = other.m_current_block_begin;
      m_current_block_idx   = other.m_current_block_idx;
    } else {
      clear();
      for (auto& elem : other) {
        emplace_back(std::move(elem));
      }
    }

    other.m_size_prefix = 0;
    other.m_capacity    = 0;
    other.m_spine.clear();
    other.m_push_cursor         = nullptr;
    other.m_push_limit          = nullptr;
    other.m_current_block_begin = nullptr;
    return *this;
  }

  // -------------------------------------------------------------------------
  // Access & Size
  // -------------------------------------------------------------------------

  [[nodiscard]] size_type size() const noexcept
  {
    if (!m_push_cursor) {
      return 0;
    }
    // Size = (Elements in prev blocks) + (Elements in current block)
    return m_size_prefix +
           static_cast<size_type>(m_push_cursor - m_current_block_begin);
  }

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
    if (index >= size()) {
      throw std::out_of_range("segmented_vector::at");
    }
    return (*this)[index];
  }

  [[nodiscard]] const_reference at(size_type index) const
  {
    if (index >= size()) {
      throw std::out_of_range("segmented_vector::at");
    }
    return (*this)[index];
  }

  [[nodiscard]] reference front() noexcept { return (*this)[0]; }

  [[nodiscard]] const_reference front() const noexcept { return (*this)[0]; }

  [[nodiscard]] reference back() noexcept { return *(m_push_cursor - 1); }

  [[nodiscard]] const_reference back() const noexcept
  {
    return *(m_push_cursor - 1);
  }

  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  [[nodiscard]] size_type max_size() const noexcept
  {
    return AllocTraits::max_size(m_allocator);
  }

  [[nodiscard]] size_type capacity() const noexcept { return m_capacity; }

  void clear() noexcept
  {
    size_type current_sz = size();
    for (size_type i = current_sz; i > 0; --i) {
      auto [k, off] = get_location(i - 1);
      AllocTraits::destroy(m_allocator, m_spine[k] + off);
    }
    // Capacity preserved. Reset cursors to start of Block 0.
    reset_cursors();
  }

  // -------------------------------------------------------------------------
  // Hyper-Optimized Push Back
  // -------------------------------------------------------------------------
  void push_back(const T& value) { emplace_back(value); }

  void push_back(T&& value) { emplace_back(std::move(value)); }

  template <typename... Args> reference emplace_back(Args&&... args)
  {
    // FAST PATH:
    // 1. Branch
    // 2. Construct
    // 3. Increment Cursor
    // (No size increment, no size load/store)
    if (m_push_cursor != m_push_limit) [[likely]] {
      AllocTraits::construct(
          m_allocator, m_push_cursor, std::forward<Args>(args)...
      );
      reference ret = *m_push_cursor;
      ++m_push_cursor;
      return ret;
    }

    return emplace_back_slow(std::forward<Args>(args)...);
  }

  // -------------------------------------------------------------------------
  // High-Performance Block Iteration
  // -------------------------------------------------------------------------

  // Applies func(const T& element) to every element.
  // loops over the contiguous segments directly, enabling
  // SIMD/Auto-vectorization.
  template <typename Func> void for_each_segment(Func&& func) const
  {
    if (empty()) {
      return;
    }

    // 1. Process fully filled blocks
    // The last block might be partial, so we handle it separately or carefully.
    // Actually, we know the size of every block.

    // Block 0
    if (!m_spine.empty()) {
      size_type count = (m_current_block_idx == 0)
                            ? static_cast<size_type>(m_push_cursor - m_spine[0])
                            : get_block_capacity(0);
      T*        ptr   = m_spine[0];
      for (size_type i = 0; i < count; ++i) {
        func(ptr[i]);
      }
    }

    // Blocks 1 to k
    for (size_type k = 1; k <= m_current_block_idx; ++k) {
      T* ptr = m_spine[k];
      // If this is the current (last) block, calculate used size
      size_type capacity = get_block_capacity(k);
      size_type count    = (k == m_current_block_idx)
                               ? static_cast<size_type>(m_push_cursor - ptr)
                               : capacity;

      // This inner loop over raw pointers will be Auto-Vectorized by the
      // compiler
      for (size_type i = 0; i < count; ++i) {
        func(ptr[i]);
      }
    }
  }

private:
  template <typename... Args>
  [[gnu::noinline]] reference emplace_back_slow(Args&&... args)
  {
    size_type current_sz = size();
    if (current_sz < m_capacity) {
      // Next block already exists. Hop to it.
      // Commit size of the block we just finished to prefix
      if (m_current_block_begin) {
        m_size_prefix += get_block_capacity(m_current_block_idx);
      }

      ++m_current_block_idx;
      m_current_block_begin = m_spine[m_current_block_idx];
      m_push_cursor         = m_current_block_begin;
      m_push_limit = m_push_cursor + get_block_capacity(m_current_block_idx);
    } else {
      grow();
      // grow updates cursors and size prefix
    }

    AllocTraits::construct(
        m_allocator, m_push_cursor, std::forward<Args>(args)...
    );
    reference ret = *m_push_cursor;
    ++m_push_cursor;
    return ret;
  }

  void grow()
  {
    // Before growing, ensure prefix is updated for full blocks up to now
    if (m_push_cursor && m_push_cursor == m_push_limit) {
      m_size_prefix += get_block_capacity(m_current_block_idx);
    }

    size_type next_idx  = m_spine.size();
    size_type next_size = get_block_capacity(next_idx);
    BlockPtr  new_block = AllocTraits::allocate(m_allocator, next_size);

    try {
      m_spine.push_back(new_block);
    } catch (...) {
      AllocTraits::deallocate(m_allocator, new_block, next_size);
      throw;
    }
    m_capacity += next_size;

    m_current_block_idx   = next_idx;
    m_current_block_begin = new_block;
    m_push_cursor         = new_block;
    m_push_limit          = new_block + next_size;
  }

  void deallocate_all_blocks()
  {
    for (size_type i = 0; i < m_spine.size(); ++i) {
      AllocTraits::deallocate(m_allocator, m_spine[i], get_block_capacity(i));
    }
    m_spine.clear();
    m_capacity            = 0;
    m_push_cursor         = nullptr;
    m_push_limit          = nullptr;
    m_current_block_begin = nullptr;
    m_size_prefix         = 0;
  }

  void copy_from(const segmented_vector& other)
  {
    while (m_capacity < other.size()) {
      grow();
    }
    if (size() == 0 && m_capacity > 0) {
      reset_cursors();
    }
    for (const auto& item : other) {
      emplace_back(item);
    }
  }

public:
  // -------------------------------------------------------------------------
  // Iterator Factories
  // -------------------------------------------------------------------------
  [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }

  [[nodiscard]] const_iterator begin() const noexcept
  {
    return const_iterator(this, 0);
  }

  [[nodiscard]] const_iterator cbegin() const noexcept
  {
    return const_iterator(this, 0);
  }

  [[nodiscard]] iterator end() noexcept { return iterator(this, size()); }

  [[nodiscard]] const_iterator end() const noexcept
  {
    return const_iterator(this, size());
  }

  [[nodiscard]] const_iterator cend() const noexcept
  {
    return const_iterator(this, size());
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

  void swap(segmented_vector& other) noexcept(
      AllocTraits::propagate_on_container_swap::value ||
      AllocTraits::is_always_equal::value
  )
  {
    using std::swap;
    if constexpr (AllocTraits::propagate_on_container_swap::value) {
      swap(m_allocator, other.m_allocator);
    }
    swap(m_spine, other.m_spine);
    swap(m_size_prefix, other.m_size_prefix);
    swap(m_capacity, other.m_capacity);
    swap(m_push_cursor, other.m_push_cursor);
    swap(m_push_limit, other.m_push_limit);
    swap(m_current_block_begin, other.m_current_block_begin);
    swap(m_current_block_idx, other.m_current_block_idx);
  }

  // -------------------------------------------------------------------------
  // Iterator Implementation
  // -------------------------------------------------------------------------
  template <bool IsConst> class iterator_impl {
    friend class segmented_vector;
    using ContainerPtr =
        std::conditional_t<IsConst, const segmented_vector*, segmented_vector*>;

    ContainerPtr m_cont;
    size_type    m_global_index;
    T*           m_current_ptr   = nullptr;
    T*           m_block_end_ptr = nullptr;

    void update_cache()
    {
      if (!m_cont || m_global_index >= m_cont->size()) {
        m_current_ptr   = nullptr;
        m_block_end_ptr = nullptr;
        return;
      }
      auto [k, off]   = m_cont->get_location(m_global_index);
      T* block_start  = m_cont->m_spine[k];
      m_current_ptr   = block_start + off;
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
    using iterator_concept  = std::random_access_iterator_tag;
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = std::conditional_t<IsConst, const T*, T*>;
    using reference         = std::conditional_t<IsConst, const T&, T&>;

    [[nodiscard]] iterator_impl() noexcept
        : m_cont(nullptr)
        , m_global_index(0)
    {}

    template <
        bool OtherConst,
        typename = std::enable_if_t<IsConst && !OtherConst>>
    [[nodiscard]] iterator_impl(const iterator_impl<OtherConst>& other) noexcept
        : m_cont(other.m_cont)
        , m_global_index(other.m_global_index)
    {
      update_cache();
    }

    [[nodiscard]] reference operator*() const { return *m_current_ptr; }

    [[nodiscard]] pointer operator->() const { return m_current_ptr; }

    [[nodiscard]] reference operator[](difference_type n) const
    {
      return (*m_cont)[m_global_index + n];
    }

    iterator_impl& operator++()
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

    iterator_impl& operator--()
    {
      if (m_global_index == 0) {
        return *this;
      }
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

    iterator_impl& operator+=(difference_type n)
    {
      m_global_index += n;
      update_cache();
      return *this;
    }

    iterator_impl& operator-=(difference_type n)
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
    operator-(const iterator_impl& lhs, const iterator_impl& rhs)
    {
      return static_cast<difference_type>(lhs.m_global_index) -
             static_cast<difference_type>(rhs.m_global_index);
    }

    [[nodiscard]] friend bool
    operator==(const iterator_impl& lhs, const iterator_impl& rhs)
    {
      return lhs.m_global_index == rhs.m_global_index;
    }

    [[nodiscard]] friend std::strong_ordering
    operator<=>(const iterator_impl& lhs, const iterator_impl& rhs)
    {
      return lhs.m_global_index <=> rhs.m_global_index;
    }
  };
};

template <typename T, typename Alloc, typename IC>
void swap(
    segmented_vector<T, Alloc, IC>& lhs, segmented_vector<T, Alloc, IC>& rhs
) noexcept(noexcept(lhs.swap(rhs)))
{
  lhs.swap(rhs);
}
