#include "net.h"

#include "arp-cfg.h"
#include "config.h"
#include "dispatcher.h"
#include "dpdk.h"

#include <iostream>

uint32_t local_ip;
static struct rte_ether_addr known_haddrs[ARP_ENTRIES_COUNT];

// Hack to communicate between templated dispatcher and non-templated netstack
rte_mbuf* pkt_buff[DPDK_BATCH_SIZE];
uint8_t pkt_count;

static void arp_init(void)
{
  printf("arp_init()\n");
  for (int i = 0; i < ARP_ENTRIES_COUNT; i++)
    str_to_eth_addr(arp_entries[i], &known_haddrs[i]);
  printf("arp_init() finished\n");
}

struct rte_ether_addr* get_mac_addr(uint32_t ip_addr)
{
  uint32_t idx = (ip_addr & 0xff) - 1; // FIXME
  return &known_haddrs[idx];
}

int net_init()
{
  local_ip = LOCAL_IP;

  arp_init();

  return 0;
}

void process_pkt(rte_mbuf* pkt)
{
  eth_in(pkt);
}

void net_send_pkt(rte_mbuf* pkt)
{
  DPDKManager::dpdk_out(pkt);
}

void reply_pkt(rte_mbuf* pkt)
{
  rte_ether_hdr* ethh = rte_pktmbuf_mtod(pkt, rte_ether_hdr*);
  rte_ipv4_hdr* iph = reinterpret_cast<rte_ipv4_hdr*>(ethh + 1);
  rte_udp_hdr* udph = reinterpret_cast<rte_udp_hdr*>(iph + 1);

  uint32_t dst_ip = CLIENT_IP; 

  uint16_t payload_len =
    rte_be_to_cpu_16(udph->dgram_len) - sizeof(rte_udp_hdr);
  uint16_t overall_len = payload_len;

  uint16_t rand_dst_port = (rand() & 0x3ff) | 0xC00;

  // switch src dst ports
  udp_out_prepare(
    udph,
    rand_dst_port,
    rte_be_to_cpu_16(udph->src_port),
    overall_len);
  overall_len += sizeof(rte_udp_hdr);

  ip_out_prepare(
    iph,
    local_ip,
    dst_ip,
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
}

void fw_to_backup(rte_mbuf* pkt)
{
  rte_ether_hdr* ethh = rte_pktmbuf_mtod(pkt, rte_ether_hdr*);
  rte_ipv4_hdr* iph = reinterpret_cast<rte_ipv4_hdr*>(ethh + 1);
  rte_udp_hdr* udph = reinterpret_cast<rte_udp_hdr*>(iph + 1);

  uint32_t dst_ip = BACKUP_IP;

  uint16_t payload_len =
    rte_be_to_cpu_16(udph->dgram_len) - sizeof(rte_udp_hdr);
  uint16_t overall_len = payload_len;

  udp_out_prepare(
    udph, 
    rte_be_to_cpu_16(udph->dst_port),
    BACKUP_UDP_PORT,
    overall_len);
  overall_len += sizeof(rte_udp_hdr);

  ip_out_prepare(
    iph,
    local_ip,
    dst_ip,
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
}

void udp_pkt_process(struct rte_mbuf* pkt)
{
  pkt_buff[pkt_count++] = pkt;
}
