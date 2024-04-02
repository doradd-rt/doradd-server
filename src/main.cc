#include <cpp/when.h>
#include <dpdk.h>
#include <iostream>
#include <verona.h>

using namespace verona::rt;
using namespace verona::cpp;

struct rte_mempool *pktmbuf_pool;

int main(int argc, char **argv) {

  printf("Hello dorad DPDK server\n");

  DPDKManager::dpdk_init(&argc, &argv);

  printf("There are %d cores\n", rte_lcore_count());

  // Use the last lcores to schedule what you need
  // FIXME

  // One dispatcher core and the rest are for the runtime
  uint8_t nr_cpu = rte_lcore_count() - 1;

  auto &sched = Scheduler::get();
  Scheduler::set_detect_leaks(true);
  sched.set_fair(true);
  sched.init(nr_cpu);

  when() << [] {
    std::cout << "hello behaviours1\n";
    while (1)
      ;
  };
  when() << [] {
    std::cout << "hello behaviours2\n";
    while (1)
      ;
  };

  when() << [] {
    std::cout << "hello behaviours3\n";
    while (1)
      ;
  };

  std::cout << "Will now run the scheduluer\n";

  // The verona runtime will use the first lcores
  sched.run();
}

// Temporary here
void process_pkt(rte_mbuf *pkt) { std::cout << "Will process pkt\n"; }
