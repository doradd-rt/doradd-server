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

// FIXME: should also be templated
int workload_init()
{
  YCSBTransaction::index = new Index<YCSBRow>;

  /* uint64_t cown_prev_addr = 0; */
  /* uint8_t* cown_arr_addr = static_cast<uint8_t*>(aligned_alloc_hpage( */
  /*       1024 * DB_SIZE)); */

  for (int i = 0; i < DB_SIZE; i++)
  {
    /* cown_ptr<YCSBRow> cown_r = make_cown_custom<YCSBRow>( */
    /*     reinterpret_cast<void *>(cown_arr_addr + (uint64_t)1024 * i)); */

    /* if (i > 0) */
    /*   assert((cown_r.get_base_addr() - cown_prev_addr) == 1024); */
    /* cown_prev_addr = cown_r.get_base_addr(); */
    
    cown_ptr<YCSBRow> cown_r = make_cown<YCSBRow>();

    YCSBTransaction::index->insert_row(cown_r);
  }

  return 0;
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
