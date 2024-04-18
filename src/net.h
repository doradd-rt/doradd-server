#pragma once

#include "config.h"

#include <stdlib.h>

extern "C"
{
#include <rte_arp.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_icmp.h>
#include <rte_ip.h>
#include <rte_udp.h>
}

extern uint32_t local_ip;
extern const char* arp_entries[];

struct __attribute__((__packed__)) custom_rpc_header {
  uint64_t sent_timestamp;

public:
  void set(uint64_t timestamp) { sent_timestamp = timestamp; }
  uint64_t get() { return sent_timestamp; }
};

int net_init(void);
static inline void eth_in(struct rte_mbuf* pkt);
static inline void eth_out_prepare(
  struct rte_ether_hdr* ethh,
  uint16_t h_proto,
  struct rte_ether_addr* dst_haddr);
static inline void ip_in(struct rte_mbuf* pkt);
static inline void ip_out_prepare(
  struct rte_ipv4_hdr* iph,
  uint32_t src_ip,
  uint32_t dst_ip,
  uint8_t ttl,
  uint8_t tos,
  uint8_t proto,
  uint16_t l4len);
static inline void arp_in(struct rte_mbuf* pkt);
static inline void icmp_in(struct rte_mbuf* pkt);
static inline void udp_out_prepare(
  struct rte_udp_hdr* udph, uint16_t src_port, uint16_t dst_port, uint16_t len);
struct rte_ether_addr* get_mac_addr(uint32_t ip_addr);
static void get_local_mac(rte_ether_addr* dst);

// Need to implement that
void net_send_pkt(rte_mbuf* pkt);
void reply_pkt(rte_mbuf* pkt);
void udp_pkt_process(struct rte_mbuf* pkt);

static inline void icmp_echo(struct rte_mbuf* pkt)
{
  int iphlen;
  int icmplen;

  struct rte_ether_hdr* ethh = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr*);
  struct rte_ipv4_hdr* iph = rte_pktmbuf_mtod_offset(
    pkt, struct rte_ipv4_hdr*, sizeof(struct rte_ether_hdr));
  struct rte_icmp_hdr* icmph = rte_pktmbuf_mtod_offset(
    pkt,
    struct rte_icmp_hdr*,
    sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

  /* compute icmp length */
  iphlen = (iph->version_ihl & RTE_IPV4_HDR_IHL_MASK) * RTE_IPV4_IHL_MULTIPLIER;
  icmph->icmp_type = RTE_IP_ICMP_ECHO_REPLY;

  icmplen = rte_be_to_cpu_16(iph->total_length) - iphlen;

  icmph->icmp_cksum = 0;
  icmph->icmp_cksum = rte_raw_cksum((void*)icmph, icmplen);
  icmph->icmp_cksum = (icmph->icmp_cksum == 0xffff) ?
    icmph->icmp_cksum :
    (uint16_t) ~(icmph->icmp_cksum);

  ip_out_prepare(
    iph,
    rte_be_to_cpu_32(iph->dst_addr),
    rte_be_to_cpu_32(iph->src_addr),
    iph->time_to_live,
    iph->type_of_service,
    IPPROTO_ICMP,
    icmplen);
  eth_out_prepare(ethh, RTE_ETHER_TYPE_IPV4, &ethh->src_addr);

  /* Send the packet */
  pkt->data_len = sizeof(struct rte_ether_hdr) + iphlen + icmplen;
  pkt->pkt_len = pkt->data_len;
  net_send_pkt(pkt);
}

static inline void icmp_in(struct rte_mbuf* pkt)
{
  struct rte_icmp_hdr* icmph = rte_pktmbuf_mtod_offset(
    pkt,
    struct rte_icmp_hdr*,
    sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

  if (icmph->icmp_type == RTE_IP_ICMP_ECHO_REQUEST)
    icmp_echo(pkt);
  else
  {
    fprintf(stderr, "Wrong ICMP type: %d\n", icmph->icmp_type);
    rte_pktmbuf_free(pkt);
  }
}

static inline void ip_in(struct rte_mbuf* pkt)
{
  struct rte_ipv4_hdr* iph;

  iph = rte_pktmbuf_mtod_offset(
    pkt, struct rte_ipv4_hdr*, sizeof(struct rte_ether_hdr));

  if (rte_be_to_cpu_32(iph->dst_addr) != local_ip)
    goto out;

  switch (iph->next_proto_id)
  {
    case IPPROTO_UDP:
      udp_pkt_process(pkt);
      break;
    case IPPROTO_ICMP:
      icmp_in(pkt);
      break;
    default:
      goto out;
  }

  return;

out:
  printf("Wrong msg: %u, %u\n", rte_be_to_cpu_32(iph->dst_addr),rte_be_to_cpu_32(iph->src_addr));
  fprintf(stderr, "UNKNOWN L3 PROTOCOL OR WRONG DST IP\n");
  rte_pktmbuf_free(pkt);
}

static inline void ip_out_prepare(
  struct rte_ipv4_hdr* iph,
  uint32_t src_ip,
  uint32_t dst_ip,
  uint8_t ttl,
  uint8_t tos,
  uint8_t proto,
  uint16_t l4len)
{
  int hdrlen;

  hdrlen = sizeof(struct rte_ipv4_hdr);
  if (proto == IPPROTO_IGMP)
    hdrlen += 4; // 4 bytes for options

  /* setup ip hdr */
  iph->version_ihl = (4 << 4) | (hdrlen / RTE_IPV4_IHL_MULTIPLIER);
  iph->type_of_service = tos;
  iph->total_length = rte_cpu_to_be_16(hdrlen + l4len);
  iph->packet_id = 0;
  iph->fragment_offset = rte_cpu_to_be_16(0x4000); // Don't fragment
  iph->time_to_live = ttl;
  iph->next_proto_id = proto;
  iph->hdr_checksum = 0;
  iph->src_addr = rte_cpu_to_be_32(src_ip);
  iph->dst_addr = rte_cpu_to_be_32(dst_ip);

  /* Mark ECN enabled */
  iph->type_of_service |= 0x1;

  /* setup checksum */
  iph->hdr_checksum = rte_raw_cksum(iph, hdrlen);
  iph->hdr_checksum = (iph->hdr_checksum == 0xffff) ?
    iph->hdr_checksum :
    (uint16_t) ~(iph->hdr_checksum);
}

static void arp_reply(struct rte_mbuf* pkt, struct rte_arp_hdr* arph)
{
  arph->arp_opcode = rte_cpu_to_be_16(RTE_ARP_OP_REPLY);

  /* fill arp body */
  arph->arp_data.arp_tip = arph->arp_data.arp_sip;
  arph->arp_data.arp_sip = rte_cpu_to_be_32(local_ip);

  arph->arp_data.arp_tha = arph->arp_data.arp_sha;
  get_local_mac(&arph->arp_data.arp_sha);

  struct rte_ether_hdr* ethh = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr*);
  eth_out_prepare(ethh, RTE_ETHER_TYPE_ARP, &arph->arp_data.arp_tha);
  pkt->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_arp_hdr);
  pkt->pkt_len = pkt->data_len;
  net_send_pkt(pkt);
}

void arp_in(struct rte_mbuf* pkt)
{
  struct rte_arp_hdr* arph = rte_pktmbuf_mtod_offset(
    pkt, struct rte_arp_hdr*, sizeof(struct rte_ether_hdr));

  /* process only arp for this address */
  if (rte_be_to_cpu_32(arph->arp_data.arp_tip) != local_ip)
    goto OUT;

  switch (rte_be_to_cpu_16(arph->arp_opcode))
  {
    case RTE_ARP_OP_REQUEST:
      arp_reply(pkt, arph);
      break;
    default:
      fprintf(stderr, "apr: Received unknown ARP op");
      goto OUT;
  }

  return;

OUT:
  rte_pktmbuf_free(pkt);
  return;
}

static inline void eth_in(struct rte_mbuf* pkt)
{
  struct rte_ether_hdr* hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr*);

  switch (rte_be_to_cpu_16(hdr->ether_type))
  {
    case RTE_ETHER_TYPE_IPV4:
      ip_in(pkt);
      break;
    case RTE_ETHER_TYPE_ARP:
      arp_in(pkt);
      break;
    default:
      rte_pktmbuf_free(pkt);
  }
}

static inline void eth_out_prepare(
  struct rte_ether_hdr* ethh,
  uint16_t h_proto,
  struct rte_ether_addr* dst_haddr)
{
  if (ethh == NULL || dst_haddr == NULL)
  {
    rte_exit(
      EXIT_FAILURE, "eth_out_prepare() received NULL argument. Exiting\n");
  }
  /* fill the ethernet header */
  ethh->dst_addr = *dst_haddr;
  ethh->ether_type = rte_cpu_to_be_16(h_proto);
  get_local_mac(&ethh->src_addr);
}

static inline void udp_out_prepare(
  struct rte_udp_hdr* udph, uint16_t src_port, uint16_t dst_port, uint16_t len)
{
  udph->dgram_cksum = 0;
  udph->dgram_len = rte_cpu_to_be_16(len + sizeof(struct rte_udp_hdr));
  udph->src_port = rte_cpu_to_be_16(src_port);
  udph->dst_port = rte_cpu_to_be_16(dst_port);
}

static inline void get_local_mac(rte_ether_addr* dst)
{
  // This does not work with the TAP device
  rte_eth_macaddr_get(0, dst);
  //char local_mac[] = {0xde, 0xad, 0xbe, 0xef, 0x5e, 0xb1};
  //memcpy(dst, local_mac, 6);
}

static inline uint32_t ip_str_to_int(const char* ip)
{
  uint32_t addr;
  unsigned char a, b, c, d;
  if (sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4)
  {
    return -EINVAL;
  }
  addr = RTE_IPV4(a, b, c, d);
  return addr;
}

static inline int str_to_eth_addr(const char* src, struct rte_ether_addr* dst)
{
  if (
    sscanf(
      src,
      "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
      &dst->addr_bytes[0],
      &dst->addr_bytes[1],
      &dst->addr_bytes[2],
      &dst->addr_bytes[3],
      &dst->addr_bytes[4],
      &dst->addr_bytes[5]) != 6)
    return -1;
  return 0;
}
