#pragma once

#include <cpp/when.h>
#include <dpdk.h>
#include <iostream>
#include <net.h>
#include <verona.h>

extern "C"
{
#include <rte_flow.h>
}

using namespace verona::cpp;

extern rte_mbuf* pkt_buff[DPDK_BATCH_SIZE];
extern uint8_t pkt_count;

template<typename T>
class Dispatcher
{
  static const uint32_t BUFFER_SIZE = 512; // should be a power of 2

  uint32_t curr_idx;
  uint32_t dispatch_idx;
  T buffer[BUFFER_SIZE];

  void configure_rx_queue()
  {
    // Receive all packets in the dispatcher queue
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

  void dispatch()
  {
    // FIXME: Just schedule the calls for now.
    // Should call the transactions eventually
    while (dispatch_idx < curr_idx)
    {
      rte_mbuf* pkt = reinterpret_cast<rte_mbuf*>(buffer[dispatch_idx]);
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
  }

  void parse_pkts()
  {
    for (int i = 0; i < pkt_count; i++)
    {
      // FIXME: Just keep the packet address for now
      // Parsing should populate an app-specific transaction stuct, hence T
      buffer[curr_idx++ & (BUFFER_SIZE - 1)] =
        reinterpret_cast<uint64_t>(pkt_buff[i]);
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

      // Dispatch the parsed packets
      // FIXME: For now schedule directly
      dispatch();
    }

    return 0;
  }

  Dispatcher() : curr_idx(0), dispatch_idx(0)
  {
    bzero(pkt_buff, sizeof(pkt_buff));
    pkt_count = 0;

    // FIXME: Schedule the other dispathcer threads here
  }

public:
  void process_pkt(rte_mbuf* pkt)
  {
    Dispatcher::process_pkt(pkt);
  }

  static Dispatcher* dispatcher_ptr;

  static int main(void* arg)
  {
    uint8_t q = reinterpret_cast<uint64_t>(arg);

    RTE_PER_LCORE(queue_id) = q;
    std::cout << "Hello from the dispatcher. Local queue " << q << std::endl;

    Dispatcher d;
    d.main();

    return 0;
  }
};
