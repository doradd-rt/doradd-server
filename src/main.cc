#include <cpp/when.h>
#include <dispatcher.h>
#include <dpdk.h>
#include <iostream>
#include <net.h>
#include <verona.h>

#include "db.h"
#include "ycsb.h"

using namespace verona::rt;
using namespace verona::cpp;

struct rte_mempool* pktmbuf_pool;
RTE_DEFINE_PER_LCORE(uint8_t, queue_id);

int workload_init()
{
  return YCSB::init();
}

int main(int argc, char** argv)
{
  printf("Hello dorad DPDK server\n");

  DPDKManager::dpdk_init(&argc, &argv);

  net_init();

  workload_init();

  printf("There are %d cores\n", rte_lcore_count());

  uint8_t worker_count = rte_lcore_count() - DISPATCHER_CORES;

  // The first worker_count cores will be the workers and the last
  // DISPATCHER_CORES will be for the dispatcher 
  int lcore_id, count = 0;
  RTE_LCORE_FOREACH_WORKER(lcore_id)
  {
    if (++count < worker_count)
      continue;
    std::cout << "The rpc handler will run on " << lcore_id << std::endl;
    rte_eal_remote_launch(
      RPCHandler<YCSBTransaction>::main, reinterpret_cast<void*>(count), lcore_id);
    break;
  }

  auto& sched = Scheduler::get();
  Scheduler::set_detect_leaks(true);
  sched.set_fair(true);
  sched.init(worker_count);

  when() << []() {
    std::cout << "Add external event source\n";
    Scheduler::add_external_event_source();
  };

  std::cout << "Will now run the scheduluer\n";

  // The verona runtime will use the first lcores
  RTE_PER_LCORE(queue_id) = 0;
  sched.run();
}
