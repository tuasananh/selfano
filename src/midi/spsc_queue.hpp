#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace selfano::midi {

// Lock-free, allocation-free, cache-line-padded single-producer / single-consumer ring buffer.
template <typename T, std::size_t Capacity> class SpscQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                "SpscQueue Capacity must be a power of two >= 2");
  static_assert(std::is_trivially_copyable_v<T>,
                "SpscQueue element type must be trivially copyable for RT safety");

public:
  SpscQueue() noexcept = default;
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  [[nodiscard]] bool try_push(const T& item) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= Capacity) {
      return false;
    }
    buffer_[tail & kMask] = item;
    tail_.store(tail + 1U, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(T& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) {
      return false;
    }
    out = buffer_[head & kMask];
    head_.store(head + 1U, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool empty() const noexcept {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t size() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    const std::size_t head = head_.load(std::memory_order_acquire);
    return tail - head;
  }

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
  static constexpr std::size_t kMask = Capacity - 1U;
  static constexpr std::size_t kCacheLineBytes = 64U;
  static constexpr std::size_t kAtomicPadBytes =
      (sizeof(std::atomic<std::size_t>) < kCacheLineBytes)
          ? (kCacheLineBytes - sizeof(std::atomic<std::size_t>))
          : 1U;

  std::atomic<std::size_t> head_{0};
  [[maybe_unused]] std::array<std::byte, kAtomicPadBytes> pad_head_{};
  std::atomic<std::size_t> tail_{0};
  [[maybe_unused]] std::array<std::byte, kAtomicPadBytes> pad_tail_{};
  std::array<T, Capacity> buffer_{};
};

} // namespace selfano::midi
