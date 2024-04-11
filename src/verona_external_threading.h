#pragma once
#include <dpdk.h>
#include <iostream>
#include <memory>

extern "C"
{
#include <rte_lcore.h>
}

class PlatformThread
{
  template<typename ThreadArgs>
  static int proxy_fn(void* args_ptr)
  {
    auto args = static_cast<ThreadArgs*>(args_ptr);

    // Deferred, exception-safe destructor.
    std::unique_ptr<ThreadArgs> thread_args_ptr(args);

    std::cout << "hello from the new verona thread. Queue id "
              << std::get<0>(*args) << std::endl;

    RTE_PER_LCORE(queue_id) = std::get<0>(*args);

    std::apply(std::get<1>(*args), std::get<2>(*args));

    return 0;
  }

public:
  template<typename F, typename... Args>
  PlatformThread(F&& f, Args&&... args)
  {
    static int worker_count = 1;
    auto fused_args = std::forward_as_tuple(args...);

    typedef std::tuple<
      int,
      typename std::decay<F>::type,
      std::tuple<typename std::decay<Args>::type...>>
      ThreadArgs;
    // Deferred, exception-safe destructor.
    auto thread_args_ptr =
      std::make_unique<ThreadArgs>(worker_count++, f, std::move(fused_args));

    static int i = -1;
    int val = rte_get_next_lcore(i, 1, 0);
    std::cout << "Creating a verona thread on lcore " << val << std::endl;
    i = val;

    rte_eal_remote_launch(
      &proxy_fn<ThreadArgs>, thread_args_ptr.release(), val);
  }

  void join()
  {
    assert(0);
  }
};

class Topology
{
public:
  size_t get(size_t index)
  {
    return index;
  }

  static void init(Topology*) noexcept {}
};

namespace cpu
{
  inline void set_affinity(size_t) {}
} // namespace cpu

inline void FlushProcessWriteBuffers()
{
  assert(0);
}
