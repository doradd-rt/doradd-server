#pragma once

#include <cpp/when.h>
#include <dpdk.h>
#include <iostream>
#include <net.h>
#include <verona.h>

#include "spscq.h"

extern "C"
{
#include <rte_flow.h>
}

#define ROWS_PER_TX 10
struct __attribute__((packed)) YCSBTransactionMarshalled
{
  uint32_t indices[ROWS_PER_TX];
  uint16_t write_set;
  uint64_t cown_ptrs[ROWS_PER_TX];
  uint8_t  pad[6];
};
static_assert(sizeof(YCSBTransactionMarshalled) == 128);

template<typename T>
struct DoradBuf
{
  uint64_t pkt_addr;
  T workload;
};

using namespace verona::cpp;

extern rte_mbuf* pkt_buff[DPDK_BATCH_SIZE];
extern uint8_t pkt_count;

#define MAX_BATCH 8

typedef std::tuple<std::atomic<uint64_t>*, uint64_t> PipelineArgs;

template<typename T>
class Indexer
{
  static const uint32_t BUFFER_SIZE = 512; // should be a power of 2
  /* uint32_t read_count; */
  /* char* read_top; */
  uint32_t dispatch_idx = 0;
  std::atomic<uint64_t>* recvd_req_cnt;
  DoradBuf<T>* global_buf;
  uint64_t handled_req_cnt;

  // inter-thread comm w/ the prefetcher
  /* rigtorp::SPSCQueue<int>* ring; */

  /* Indexer(void* mmap_ret, rigtorp::SPSCQueue<int>* ring_ */
  /*   , std::atomic<uint64_t>* req_cnt_) : */
  /*   read_top(reinterpret_cast<char*>(mmap_ret)), ring(ring_) */
  /*   , recvd_req_cnt(req_cnt_) */
  /* { */
  /*   read_count = *(reinterpret_cast<uint32_t*>(read_top)); */
  /*   read_top += sizeof(uint32_t); */
  /*   handled_req_cnt = 0; */
  /* } */

  Indexer(std::atomic<uint64_t>* req_cnt_, uint64_t global_buf_): 
    recvd_req_cnt(req_cnt_)
  {
    handled_req_cnt = 0;
    global_buf = reinterpret_cast<DoradBuf<T>*>(global_buf_);
  }

  size_t check_avail_cnts()
  {
    uint64_t avail_cnt;
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

  void dispatch()
  {
    // FIXME: Just schedule the calls for now.
    // Should call the transactions eventually
    rte_mbuf* pkt = reinterpret_cast<rte_mbuf*>(global_buf[dispatch_idx & (BUFFER_SIZE - 1)].pkt_addr);
    when() << [=]() {
      std::cout << "Will echo the packet back\n";

      rte_ether_hdr* ethh = rte_pktmbuf_mtod(pkt, rte_ether_hdr*);
      rte_ipv4_hdr* iph = reinterpret_cast<rte_ipv4_hdr*>(ethh + 1);
      rte_udp_hdr* udph = reinterpret_cast<rte_udp_hdr*>(iph + 1);

      uint32_t dst_ip = rte_be_to_cpu_32(iph->src_addr);

      uint16_t payload_len =
        rte_be_to_cpu_16(udph->dgram_len) - sizeof(rte_udp_hdr);
      uint16_t overall_len = payload_len;

      // switch src dst ports
      udp_out_prepare(
        udph,
        rte_be_to_cpu_16(udph->dst_port),
        rte_be_to_cpu_16(udph->src_port),
        overall_len);
      overall_len += sizeof(rte_udp_hdr);

      ip_out_prepare(
        iph,
        local_ip,
        rte_be_to_cpu_32(iph->src_addr),
        64,
        0,
        IPPROTO_UDP,
        overall_len);
      overall_len += sizeof(rte_ipv4_hdr);

      eth_out_prepare(ethh, RTE_ETHER_TYPE_IPV4, get_mac_addr(dst_ip));
      overall_len += sizeof(rte_ether_hdr);

      pkt->data_len = overall_len;
      pkt->pkt_len = overall_len;
      net_send_pkt(pkt);
    };

    dispatch_idx++;
  }

  void run()
  {
    /* char* read_head = read_top; */
    int i, ret = 0;
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
        /* ret = T::prepare_lowns(read_head); */
        /* read_head += ret; */
        dispatch();
      }
      
      /* ring->push(batch); */
    }
  }

public:
 static int main(void* args_ptr)
  { 
    auto args = static_cast<PipelineArgs*>(args_ptr);

    std::unique_ptr<PipelineArgs> thread_args_ptr(args);

    Indexer indexer(std::get<0>(*args), std::get<1>(*args));
    indexer.run();

    return 0;
  }
};

template<typename T>
class RPCHandler
{
  static const uint32_t BUFFER_SIZE = 512; // should be a power of 2

  uint32_t curr_idx;
  uint32_t dispatch_idx;
  std::atomic<uint64_t> req_cnt;

  DoradBuf<T> buffer[BUFFER_SIZE];

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

    ret = rte_flow_validate(0, &attr, pattern, actions, &err);
    if (ret)
    {
      std::cout << "Error creating flow : " << err.message << std::endl;
      return;
    }
    f = rte_flow_create(0, &attr, pattern, actions, &err);
    assert(f);
  }

  void parse_pkts()
  {
    for (int i = 0; i < pkt_count; i++)
    {
      // FIXME: Just keep the packet address for now
      // Parsing should populate an app-specific transaction stuct, hence T
      buffer[curr_idx++ & (BUFFER_SIZE - 1)].pkt_addr =
        reinterpret_cast<uint64_t>(pkt_buff[i]);

      req_cnt.fetch_add(1, std::memory_order_relaxed);
    }

    pkt_count = 0;
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
    }

    return 0;
  }

  RPCHandler() : curr_idx(0), dispatch_idx(0), req_cnt(0)
  {
    bzero(pkt_buff, sizeof(pkt_buff));
    pkt_count = 0;

    auto pipeline_args_ptr =
      std::make_unique<PipelineArgs>(&req_cnt, reinterpret_cast<uint64_t>(buffer));

    int lcore_id = rte_lcore_id() + 1;
    rte_eal_remote_launch(Indexer<T>::main, pipeline_args_ptr.release(), lcore_id);
    
    std::cout << "Creating next rpc handler thread on lcore " << lcore_id << std::endl;
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
