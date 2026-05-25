#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace itch {

template <typename T>
class ObjectPool {
  static_assert(std::is_trivially_destructible_v<T>,
                "ObjectPool<T> requires trivially destructible T");

 public:
  explicit ObjectPool(std::uint32_t capacity = 4'096);
  ~ObjectPool() noexcept;

  ObjectPool(const ObjectPool&) = delete;
  ObjectPool& operator=(const ObjectPool&) = delete;
  ObjectPool(ObjectPool&&) = delete;
  ObjectPool& operator=(ObjectPool&&) = delete;

  template <typename... Args>
  [[nodiscard]] T* create(Args&&... args);

  void destroy(T* object) noexcept;

  std::uint32_t capacity() const noexcept { return capacity_; }
  std::uint32_t available() const noexcept { return free_count_; }
  std::uint32_t live_count() const noexcept { return live_count_; }
  std::uint32_t high_watermark() const noexcept { return high_watermark_; }
  std::uint32_t expansion_count() const noexcept { return expansion_count_; }
  std::uint32_t block_count() const noexcept { return block_count_; }

 private:
  struct alignas(alignof(T)) Slot {
    unsigned char data[sizeof(T)];
  };

  struct Block {
    Slot* storage = nullptr;
    std::uint32_t capacity = 0;
  };

  Block* blocks_ = nullptr;
  Slot** free_slots_ = nullptr;
  std::uint32_t block_count_ = 0;
  std::uint32_t block_capacity_ = 0;
  std::uint32_t free_slots_capacity_ = 0;
  std::uint32_t capacity_ = 0;
  std::uint32_t free_count_ = 0;
  std::uint32_t live_count_ = 0;
  std::uint32_t high_watermark_ = 0;
  std::uint32_t expansion_count_ = 0;
  std::uint32_t next_block_capacity_ = 0;

  void expand();
  void add_block(std::uint32_t capacity, bool count_expansion);
  void reserve_blocks(std::uint32_t min_capacity);
  void reserve_free_slots(std::uint32_t min_capacity);
  bool owns(const T* object) const noexcept;
  static std::uint32_t next_geometric_capacity(std::uint32_t capacity) noexcept;
};

template <typename T>
ObjectPool<T>::ObjectPool(std::uint32_t capacity)
    : next_block_capacity_(capacity == 0 ? 1 : capacity) {
  add_block(capacity, false);
}

template <typename T>
ObjectPool<T>::~ObjectPool() noexcept {
  for (std::uint32_t i = 0; i < block_count_; ++i) {
    delete[] blocks_[i].storage;
  }
  delete[] blocks_;
  delete[] free_slots_;
}

template <typename T>
template <typename... Args>
T* ObjectPool<T>::create(Args&&... args) {
  if (free_count_ == 0) [[unlikely]] {
    expand();
  }

  Slot* slot = free_slots_[--free_count_];
  void* address = static_cast<void*>(slot->data);
  T* object = nullptr;
  try {
    object = new (address) T(std::forward<Args>(args)...);
  } catch (...) {
    free_slots_[free_count_++] = slot;
    throw;
  }

  ++live_count_;
  if (live_count_ > high_watermark_) {
    high_watermark_ = live_count_;
  }
  return object;
}

template <typename T>
void ObjectPool<T>::destroy(T* object) noexcept {
  if (object == nullptr) {
    return;
  }

  assert(owns(object) && "Pointer does not belong to this pool");
  assert(live_count_ > 0 && "ObjectPool double free");
  assert(free_count_ < capacity_ && "ObjectPool double free");

  free_slots_[free_count_++] = reinterpret_cast<Slot*>(object);
  --live_count_;
}

template <typename T>
void ObjectPool<T>::expand() {
  const std::uint32_t block_capacity = next_block_capacity_;
  add_block(block_capacity, true);
  next_block_capacity_ = next_geometric_capacity(block_capacity);
}

template <typename T>
void ObjectPool<T>::add_block(std::uint32_t capacity, bool count_expansion) {
  if (capacity == 0) {
    return;
  }
  if (capacity > std::numeric_limits<std::uint32_t>::max() - capacity_) {
    throw std::bad_alloc();
  }

  reserve_blocks(block_count_ + 1);
  reserve_free_slots(capacity_ + capacity);

  Slot* storage = new Slot[capacity];
  blocks_[block_count_++] = Block{storage, capacity};
  for (std::uint32_t i = 0; i < capacity; ++i) {
    free_slots_[free_count_++] = &storage[capacity - 1 - i];
  }
  capacity_ += capacity;
  if (count_expansion) {
    ++expansion_count_;
  }
}

template <typename T>
void ObjectPool<T>::reserve_blocks(std::uint32_t min_capacity) {
  if (min_capacity <= block_capacity_) {
    return;
  }

  std::uint32_t new_capacity = block_capacity_ == 0 ? 1 : block_capacity_;
  while (new_capacity < min_capacity) {
    const std::uint32_t previous = new_capacity;
    new_capacity = next_geometric_capacity(new_capacity);
    if (new_capacity == previous) {
      throw std::bad_alloc();
    }
  }

  Block* next = new Block[new_capacity];
  for (std::uint32_t i = 0; i < block_count_; ++i) {
    next[i] = blocks_[i];
  }
  delete[] blocks_;
  blocks_ = next;
  block_capacity_ = new_capacity;
}

template <typename T>
void ObjectPool<T>::reserve_free_slots(std::uint32_t min_capacity) {
  if (min_capacity <= free_slots_capacity_) {
    return;
  }

  std::uint32_t new_capacity = free_slots_capacity_ == 0 ? 1 : free_slots_capacity_;
  while (new_capacity < min_capacity) {
    const std::uint32_t previous = new_capacity;
    new_capacity = next_geometric_capacity(new_capacity);
    if (new_capacity == previous) {
      throw std::bad_alloc();
    }
  }

  Slot** next = new Slot*[new_capacity];
  for (std::uint32_t i = 0; i < free_count_; ++i) {
    next[i] = free_slots_[i];
  }
  delete[] free_slots_;
  free_slots_ = next;
  free_slots_capacity_ = new_capacity;
}

template <typename T>
bool ObjectPool<T>::owns(const T* object) const noexcept {
  const auto ptr = reinterpret_cast<std::uintptr_t>(object);
  for (std::uint32_t i = 0; i < block_count_; ++i) {
    const auto base = reinterpret_cast<std::uintptr_t>(blocks_[i].storage);
    const auto bytes = static_cast<std::uintptr_t>(blocks_[i].capacity) * sizeof(Slot);
    const auto end = base + bytes;
    if (ptr >= base && ptr < end) {
      const auto offset = ptr - base;
      return offset % sizeof(Slot) == 0;
    }
  }
  return false;
}

template <typename T>
std::uint32_t ObjectPool<T>::next_geometric_capacity(
    std::uint32_t capacity) noexcept {
  const std::uint32_t max = std::numeric_limits<std::uint32_t>::max();
  if (capacity == 0) {
    return 1;
  }
  if (capacity > max / 2) {
    return max;
  }
  return capacity * 2;
}

}  // namespace itch
