#pragma once

#include <cpp/when.h>
#include <dpdk.h>
#include <iostream>
#include <net.h>
#include <verona.h>

#include "SPSCQueue.h"

extern "C"
{
#include <rte_flow.h>
}

template<typename T>
struct DoradBuf
{
  uint64_t pkt_addr;
  typename T::Marshalled workload;
};

using namespace verona::cpp;

extern rte_mbuf* pkt_buff[DPDK_BATCH_SIZE];
extern uint8_t pkt_count;

#define MAX_BATCH 8
static const uint32_t BUFFER_SIZE = 512 * 64; // should be a power of 2

typedef std::tuple<std::atomic<uint64_t>*, uint64_t> PipelineArgs;
typedef rigtorp::SPSCQueue<int> InterCore;

typedef std::tuple<std::atomic<uint64_t>*, InterCore*, uint64_t> IndexerArgs;
typedef std::tuple<InterCore*, InterCore*, uint64_t> PrefetcherArgs;
typedef std::tuple<InterCore*, uint64_t> SpawnerArgs;

template<typename T>
class BaseDispatcher
{
protected:
  DoradBuf<T>* global_buf;

  BaseDispatcher(uint64_t global_buf_)
  {
    global_buf = reinterpret_cast<DoradBuf<T>*>(global_buf_);
  }

  virtual void run() = 0; 
};

template<typename T>
class Indexer : BaseDispatcher<T>
{
  uint64_t handled_req_cnt;

  // member fields for inter-core communication
  std::atomic<uint64_t>* recvd_req_cnt;
  InterCore* queue;

  size_t check_avail_cnts()
  {
    uint64_t avail_cnt = 0;
    size_t dyn_batch;

    do {
      uint64_t load_val = recvd_req_cnt->load(std::memory_order_relaxed);
      avail_cnt = load_val - handled_req_cnt;
      if (avail_cnt >= MAX_BATCH) 
        dyn_batch = MAX_BATCH;
      else if (avail_cnt > 0)
        dyn_batch = static_cast<size_t>(avail_cnt);
      else
      {
        _mm_pause();
        continue;  
      }
    } while (avail_cnt == 0);

    handled_req_cnt += dyn_batch;

    return dyn_batch;
  }

  void run()
  {
    char* read_head = reinterpret_cast<char*>(BaseDispatcher<T>::global_buf);
    int i, ret, batch, curr_idx = 0;

    while(1)
    {
      batch = check_avail_cnts();

      for (i = 0; i < batch; i++)
      {
        read_head = reinterpret_cast<char*>(
          &BaseDispatcher<T>::global_buf[curr_idx++ & (BUFFER_SIZE - 1)]);
        ret = T::prepare_cowns(read_head);
      }

      queue->push(batch);
    }
  }

  Indexer(std::atomic<uint64_t>* req_cnt_, InterCore* queue_, uint64_t global_buf_) : 
    BaseDispatcher<T>(global_buf_), handled_req_cnt(0), recvd_req_cnt(req_cnt_), queue(queue_) {}

public:
  static int main(void* args_ptr)
  {
    auto args = static_cast<IndexerArgs*>(args_ptr);

    std::unique_ptr<IndexerArgs> thread_args_ptr(args);

    Indexer indexer(std::get<0>(*args), std::get<1>(*args), std::get<2>(*args));
    indexer.run();

    return 0;
  }
};

template<typename T>
class Prefetcher: BaseDispatcher<T>
{
  // member fields for inter-core communication
  InterCore* in_queue;
  InterCore* out_queue;

  void run()
  {
    char* read_head = reinterpret_cast<char*>(BaseDispatcher<T>::global_buf);
    int i, ret, batch, curr_idx = 0;

    while(1)
    {
      if (!in_queue->front())
        continue;

      batch = static_cast<int>(*in_queue->front());
    
      for (i = 0; i < batch; i++)
      {
        read_head = reinterpret_cast<char*>(
          &BaseDispatcher<T>::global_buf[curr_idx++ & (BUFFER_SIZE - 1)]);
        ret = T::prefetch_cowns(read_head);
      }

      out_queue->push(batch);
      in_queue->pop();
    }
  }

  Prefetcher(InterCore* in_queue_, InterCore* out_queue_, uint64_t global_buf_) : 
     BaseDispatcher<T>(global_buf_), in_queue(in_queue_), out_queue(out_queue_) {}

public:
  static int main(void* args_ptr)
  {
    auto args = static_cast<PrefetcherArgs*>(args_ptr);

    std::unique_ptr<PrefetcherArgs> thread_args_ptr(args);

    Prefetcher prefetcher(std::get<0>(*args), std::get<1>(*args), std::get<2>(*args));
    prefetcher.run();

    return 0;
  }
};

template<typename T>
class Spawner: BaseDispatcher<T>
{
  InterCore* queue;

  void run()
  {
    char* read_head = reinterpret_cast<char*>(BaseDispatcher<T>::global_buf);
    int i, ret, batch, curr_idx = 0;

    while(1)
    {
      if (!queue->front())
        continue;

      batch = static_cast<int>(*queue->front());
    
      for (i = 0; i < batch; i++)
      {
        read_head = reinterpret_cast<char*>(
          &BaseDispatcher<T>::global_buf[curr_idx++ & (BUFFER_SIZE - 1)]);
        ret = T::parse_and_process(read_head);
      }

      queue->pop();
    }
  }

  Spawner(InterCore* queue_, uint64_t global_buf_) : 
    BaseDispatcher<T>(global_buf_), queue(queue_) {}

public:
  static int main(void* args_ptr)
  {
    auto args = static_cast<SpawnerArgs*>(args_ptr);

    std::unique_ptr<SpawnerArgs> thread_args_ptr(args);

    Spawner spawner(std::get<0>(*args), std::get<1>(*args));
    spawner.run();

    return 0;
  }
};

template<typename T>
class TestOneDispatcher
{
  /* uint32_t read_count; */
  /* char* read_top; */
  uint32_t dispatch_idx = 0;
  std::atomic<uint64_t>* recvd_req_cnt;
  DoradBuf<T>* global_buf;
  uint64_t handled_req_cnt;

  // inter-thread comm w/ the prefetcher
  /* rigtorp::SPSCQueue<int>* queue; */

  /* TestOneDispatcher(void* mmap_ret, rigtorp::SPSCQueue<int>* queue_ */
  /*   , std::atomic<uint64_t>* req_cnt_) : */
  /*   read_top(reinterpret_cast<char*>(mmap_ret)), queue(queue_) */
  /*   , recvd_req_cnt(req_cnt_) */
  /* { */
  /*   read_count = *(reinterpret_cast<uint32_t*>(read_top)); */
  /*   read_top += sizeof(uint32_t); */
  /*   handled_req_cnt = 0; */
  /* } */

  TestOneDispatcher(std::atomic<uint64_t>* req_cnt_, uint64_t global_buf_): 
    recvd_req_cnt(req_cnt_)
  {
    handled_req_cnt = 0;
    global_buf = reinterpret_cast<DoradBuf<T>*>(global_buf_);
  }

  int check_avail_cnts()
  {
    uint64_t avail_cnt;
    int dyn_batch;

    do {
      uint64_t load_val = recvd_req_cnt->load(std::memory_order_relaxed);
      avail_cnt = load_val - handled_req_cnt;
      if (avail_cnt >= MAX_BATCH) 
        dyn_batch = MAX_BATCH;
      else if (avail_cnt > 0)
        dyn_batch = static_cast<int>(avail_cnt);
      else
      {
        _mm_pause();
        continue;  
      }
    } while (avail_cnt == 0);

    handled_req_cnt += dyn_batch;

    return dyn_batch;
  }

  void dispatch()
  {
    // FIXME: Just schedule the calls for now.
    // Should call the transactions eventually
    rte_mbuf* pkt = reinterpret_cast<rte_mbuf*>(global_buf[dispatch_idx & (BUFFER_SIZE - 1)].pkt_addr);
    when() << [=]() {
      std::cout << "Will echo the packet back\n";

      reply_pkt(pkt);
    };

    /* dispatch_idx++; */
  }

  void run()
  {
    char* read_head = reinterpret_cast<char*>(global_buf);
    char* prepare_read_head = read_head;
    int i, ret, prepare_ret = 0;
    int batch = 0;

    while(1)
    {
      /* if (dispatch_idx > (read_count - batch)) */
      /* { */
      /*   read_head = read_top; */
      /*   dispatch_idx = 0; */
      /* } */

      batch = check_avail_cnts();
    
      for (i = 0; i < batch; i++)
      {
        prepare_ret = T::prepare_cowns(prepare_read_head);
        prepare_read_head += prepare_ret;
      }

      for (i = 0; i < batch; i++)
      {
        /* dispatch(); */
        ret = T::parse_and_process(read_head);
        read_head += ret;
        dispatch_idx++;
      }
      
      /* queue->push(batch); */
    }
  }

public:
 static int main(void* args_ptr)
  { 
    auto args = static_cast<PipelineArgs*>(args_ptr);

    std::unique_ptr<PipelineArgs> thread_args_ptr(args);

    TestOneDispatcher testOneDispatcher(std::get<0>(*args), std::get<1>(*args));
    testOneDispatcher.run();

    return 0;
  }
};

#define GET_READ_HEAD() \
  auto read_head = reinterpret_cast<char*>(&buffer[curr_idx++ & (BUFFER_SIZE - 1)]); \


template<typename T>
class RPCHandler
{
  static const size_t CHANNEL_SIZE = 2;

  uint32_t curr_idx;
  uint32_t dispatch_idx;
  std::atomic<uint64_t> req_cnt;

  DoradBuf<T> buffer[BUFFER_SIZE];

  InterCore channel_1{CHANNEL_SIZE}; 
  InterCore channel_2{CHANNEL_SIZE}; 

  void configure_rx_queue()
  {
    // Receive all packets in the rpc handler queue
    int ret;
    struct rte_flow* f;

    struct rte_flow_attr attr = {0};
    struct rte_flow_item pattern[2];

    struct rte_flow_action actions[2];
    struct rte_flow_action_queue queue;
    struct rte_flow_error err;

    bzero(&attr, sizeof(attr));
    bzero(pattern, sizeof(pattern));
    bzero(actions, sizeof(actions));
    bzero(&queue, sizeof(queue));
    bzero(&err, sizeof(err));

    attr.ingress = 1;
    // Allow all eth packets
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[1].type = RTE_FLOW_ITEM_TYPE_END;

    queue.index = RTE_PER_LCORE(queue_id);
    actions[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    actions[0].conf = &queue;
    actions[1].type = RTE_FLOW_ACTION_TYPE_END;

    ret = rte_flow_validate(PORT_ID, &attr, pattern, actions, &err);
    if (ret)
    {
      std::cout << "Error creating flow : " << err.message << std::endl;
      return;
    }
    f = rte_flow_create(PORT_ID, &attr, pattern, actions, &err);
    assert(f);
  }

  void parse_pkts()
  {
    for (int i = 0; i < pkt_count; i++)
    {
      rte_ether_hdr* ethh = rte_pktmbuf_mtod(pkt_buff[i], rte_ether_hdr*);
      rte_ipv4_hdr* iph = reinterpret_cast<rte_ipv4_hdr*>(ethh + 1);
      uint32_t src_ip  = rte_be_to_cpu_32(iph->src_addr);
      uint32_t idx = (src_ip & 0xff) - 1;
 
      // if src ip is client, forward it to backup
      // if src ip is backup, schedule, exec, and send back to client
      switch(idx) {
        case CLIENT_ID:
          fw_to_backup(pkt_buff[i]);
          break;
        case BACKUP_ID:
          // TODO: change mac/ip/udp, consider not using reply_pkt API
          uint64_t pktAddr = reinterpret_cast<uint64_t>(pkt_buff[i]);
          buffer[curr_idx & (BUFFER_SIZE - 1)].pkt_addr = pktAddr;
          GET_READ_HEAD();
          T::parse_pkt(read_head);
          break;
        default:
          printf("Wrong src ip\n");
      }
    }
  }

  void index_lookup()
  {
    curr_idx -= pkt_count;
    int ret;
    for (int i = 0; i < pkt_count; i++)
    {
      GET_READ_HEAD();
      ret = T::prepare_cowns(read_head);
    }
  }

  void prefetch()
  {
    curr_idx -= pkt_count;
    int ret;
    for (int i = 0; i < pkt_count; i++)
    {
      GET_READ_HEAD();
      ret = T::prefetch_cowns(read_head);
    }
  }

  void dispatch()
  {
    curr_idx -= pkt_count;
    int ret;
    for (int i = 0; i < pkt_count; i++)
    {
      GET_READ_HEAD();
      ret = T::parse_and_process(read_head);
    }
  }

  int main()
  {
    configure_rx_queue();

    std::cout << "Configured the flow director without a problem\n";

    while (1)
    {
      DPDKManager::dpdk_poll();

      // Parse a batch of received pkts
      parse_pkts();

      index_lookup();

      prefetch();

      dispatch();  
      
      pkt_count = 0;
   }

    return 0;
  }

  RPCHandler() : curr_idx(0), dispatch_idx(0), req_cnt(0)
  {
    bzero(pkt_buff, sizeof(pkt_buff));
    pkt_count = 0;

    // launch dispatcher threads

    /* auto pipeline_args_ptr = */
    /*   std::make_unique<PipelineArgs>(&req_cnt, reinterpret_cast<uint64_t>(buffer)); */

    /* int lcore_id = rte_lcore_id() + 1; */
    /* rte_eal_remote_launch(TestOneDispatcher<T>::main, pipeline_args_ptr.release(), lcore_id); */

    /* int lcore_id = rte_lcore_id(); */
    /* auto buffer_addr = reinterpret_cast<uint64_t>(buffer); */
     
    /* auto indexer_args_ptr = */
    /*   std::make_unique<IndexerArgs>(&req_cnt, &channel_1, buffer_addr); */
    /* auto prefetcher_args_ptr = */
    /*   std::make_unique<PrefetcherArgs>(&channel_1, &channel_2, buffer_addr); */
    /* auto spawner_args_ptr = */
    /*   std::make_unique<SpawnerArgs>(&channel_2, buffer_addr); */

    /* rte_eal_remote_launch(Indexer<T>::main, indexer_args_ptr.release(), lcore_id + 1); */
    /* rte_eal_remote_launch(Prefetcher<T>::main, prefetcher_args_ptr.release(), lcore_id + 2); */
    /* rte_eal_remote_launch(Spawner<T>::main, spawner_args_ptr.release(), lcore_id + 3); */

    /* std::cout << "Creating Indexer thread on lcore " << lcore_id + 1 << std::endl; */
    /* std::cout << "Creating Prefetcher thread on lcore " << lcore_id + 2 << std::endl; */
    /* std::cout << "Creating Spawner thread on lcore " << lcore_id + 3 << std::endl; */
  }

public:
  void process_pkt(rte_mbuf* pkt)
  {
    RPCHandler::process_pkt(pkt);
  }

  static int main(void* arg)
  {
    uint8_t q = reinterpret_cast<uint64_t>(arg);

    RTE_PER_LCORE(queue_id) = q;
    std::cout << "Hello from the rpc handler. Local queue " << q << std::endl;

    RPCHandler rpc_handler;
    rpc_handler.main();

    return 0;
  }
};
