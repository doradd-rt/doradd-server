#pragma once

class PlatformThread {
public:
  template <typename F, typename... Args>
  PlatformThread(F &&f, Args &&...args) {}

  void join() { assert(0); }
};

class Topology {
public:
  size_t get(size_t index) {
    assert(0);
    return index;
  }

  static void init(Topology *) noexcept { assert(0); }
};

namespace cpu {
inline void set_affinity(size_t) {}
} // namespace cpu

inline void FlushProcessWriteBuffers() { assert(0); }
