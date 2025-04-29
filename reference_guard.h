#pragma once
#include <atomic>

/**
 * Thread-safe, lock-free reference_guard management for any object's references
 *
 * Tracks references to an object across threads using atomic operations.
 * If declared in the same scope, e.g
 *   asio::io_context io;
 *   reference_guard io_guard(io);
 *   // ...
 * You can ensure the original object outlives all references by blocking in
 * destructor until all references are released. References can only be
 * obtained through make_reference() through the original reference_guard,
 * not other references. This should guarantee that no new refs can spawn when
 * reference_guard destructs.
 *
 * TIP: if you make actual refs to this, i.e., reference_guard&, you just
 * recreated the problem this class tried to solve. Don't do that. Use
 * make_reference.
 *
 * References can check if the underlying object is still valid and
 * optionally wait for its destruction. The reference_guard::references must
 * find a way to release themselves in a timely manner to unblock this object's
 * destruction process.
 *
 * This is an ad hoc solution to fixing stack allocated reference_guard issues.
 * For more general problems use shared/weak ptr.
 */
template <typename T> class reference_guard {
public:
  explicit constexpr reference_guard(T &object)
      : object_{object}, counter_(0), alive_(true) {}
  constexpr ~reference_guard() {
    alive_.store(false, std::memory_order_release);
    alive_.notify_all();

    // Synchronize 1st load, to avoid any potential wait
    auto count = counter_.load(std::memory_order_acquire);
    auto has_references = [](auto count) {
      return count > static_cast<decltype(count)>(0);
    };

    while (has_references(count)) {
      // Following loads can be relaxed. Sync already ensured by notify_one()
      counter_.wait(count, std::memory_order_relaxed);
      count = counter_.load(std::memory_order_relaxed);
    }
  }
  class reference {
  public:
    explicit constexpr reference(reference_guard &g) : guard_(g) {
      guard_.counter_.fetch_add(1, std::memory_order_relaxed);
    }
    constexpr reference(reference &&other) noexcept : guard_(other.guard_) {
      guard_.counter_.fetch_add(1, std::memory_order_relaxed);
    }

    constexpr ~reference() {
      // Ensures operations inside the reference are properly synchronized, same
      // as std::shared_ptr destructor
      auto count = guard_.counter_.fetch_sub(1, std::memory_order_acq_rel);
      if (count == 1) {
        // Only notify if there's a chance that this was the last reference
        guard_.counter_.notify_one();
      }
    }

    T &get() { return guard_.object_; }
    bool alive() const { return guard_.alive_.load(std::memory_order_acquire); }
    void wait_expiry() const {
      guard_.alive_.wait(true, std::memory_order_acquire);
    }

    // Deleted constructors
    constexpr reference &operator=(reference &&other) noexcept = delete;
    constexpr reference(const reference &) = delete;
    constexpr reference &operator=(const reference &) noexcept = delete;

  private:
    reference_guard<T> &guard_;
  };

  constexpr auto make_reference() { return reference(*this); }

private:
  T &object_;
  std::atomic<std::size_t> counter_;
  std::atomic<bool> alive_;
};

template <typename T> using reference = reference_guard<T>::reference;
template <typename T> class reference_guarded {
public:
  explicit constexpr reference_guarded() : guard_(t_) {}

  constexpr auto make_reference() { return guard_.make_reference(); }

private:
  T t_;
  reference_guard<T> guard_;
};
