#include <iostream>

#include <cpp/when.h>
#include <dpdk.h>
#include <iostream>
#include <net.h>
#include <verona.h>

#include <dispatcher.h>

using namespace verona::rt;
using namespace verona::cpp;

struct rte_mempool *pktmbuf_pool;
RTE_DEFINE_PER_LCORE(uint8_t, queue_id);

int main(int argc, char **argv) {

  printf("Hello dorad DPDK server\n");

  DPDKManager::dpdk_init(&argc, &argv);

  net_init();

  printf("There are %d cores\n", rte_lcore_count());

  // One dispatcher core and the rest are for the runtime
  uint8_t worker_count = rte_lcore_count() - DISPATCHER_CORES;

  // The first worker_count cores will be the workers and the last
  // DISPATCHER_CORES will be for the dispatcher
  int lcore_id, count = 0;
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (++count < worker_count)
      continue;
    std::cout << "The dispatcher will run on " << lcore_id << std::endl;
    rte_eal_remote_launch(Dispatcher<uint64_t>::main,
                          reinterpret_cast<void *>(count), lcore_id);
    break;
  }

  auto &sched = Scheduler::get();
  Scheduler::set_detect_leaks(true);
  sched.set_fair(true);
  sched.init(worker_count);

  when() << [] {
    std::cout << "hello behaviours1\n";
    std::cout << "local lcore id is " << rte_lcore_id() << std::endl;
    while (1)
      ;
  };
  when() << [] {
    std::cout << "hello behaviours2\n";
    std::cout << "local lcore id is " << rte_lcore_id() << std::endl;
    while (1)
      ;
  };

  when() << [] {
    std::cout << "hello behaviours3\n";
    std::cout << "local lcore id is " << rte_lcore_id() << std::endl;
    while (1)
      ;
  };

  std::cout << "Will now run the scheduluer\n";

  // The verona runtime will use the first lcores
  RTE_PER_LCORE(queue_id) = 0;
  sched.run();
}
