#include <thread>
#include <unordered_map>

#include "db.h" 
#include "dispatcher.h"

static constexpr uint32_t ROWS_PER_TX = 10;
static constexpr uint32_t ROW_SIZE = 900;
static constexpr uint32_t WRITE_SIZE = 100;
static constexpr uint64_t DB_SIZE = 10'000'000;

struct YCSBRow
{
  char payload[ROW_SIZE];
};

class YCSBTable : public Table<uint64_t, YCSBRow, DB_SIZE> {
public:
  YCSBTable() : Table<uint64_t, YCSBRow, DB_SIZE> () {
    printf("YCSB tbl size: %lu\n", DB_SIZE);
  }
};

long time_ns()
{
	struct timespec ts;
	int r = clock_gettime(CLOCK_MONOTONIC, &ts);
	assert(r == 0);
	return (ts.tv_nsec + ts.tv_sec * 1e9);
}

#define GET_COWN(_INDEX) auto&& row##_INDEX = get_cown_ptr_from_addr<YCSBRow>(reinterpret_cast<void *>(txm->workload.cown_ptrs[_INDEX]));
#define TXN(_INDEX) \
{ \
  if (write_set_l & 0x1) \
    memset(acq_row##_INDEX->payload, sum, WRITE_SIZE); \
  else \
  { \
    for (int j = 0; j < ROW_SIZE; j++) \
      sum += acq_row##_INDEX->payload[j]; \
  } \
  write_set_l >>= 1; \
}

class YCSBTransaction
{
public:
  static YCSBTable* table;

  struct __attribute__((packed)) Marshalled
  {
    uint32_t indices[ROWS_PER_TX];
    uint16_t spin_usec;
    uint64_t cown_ptrs[ROWS_PER_TX];
    uint8_t  pad[6];
  };
  static_assert(sizeof(Marshalled) == 128);
 
  static void parse_pkt(char* input)
  {
    auto txm = reinterpret_cast<DoradBuf<YCSBTransaction>*>(input);
    auto pkt = reinterpret_cast<rte_mbuf*>(txm->pkt_addr);

    Marshalled* payload = reinterpret_cast<Marshalled *>(
      reinterpret_cast<uint8_t*>(rte_pktmbuf_mtod(pkt, rte_ether_hdr*)) +
      sizeof(rte_ether_hdr) +
      sizeof(rte_ipv4_hdr) +
      sizeof(rte_udp_hdr) + 
      sizeof(custom_rpc_header)
    );

    for (int i = 0; i < ROWS_PER_TX; i++)
      txm->workload.indices[i] = payload->indices[i];

    txm->workload.spin_usec = payload->spin_usec;
  }

  static int prepare_cowns(char* input)
  {
    auto txm = reinterpret_cast<DoradBuf<YCSBTransaction>*>(input);

    for (int i = 0; i < ROWS_PER_TX; i++)
    {
      auto&& cown = table->get_row(txm->workload.indices[i]);
      txm->workload.cown_ptrs[i] = cown.get_base_addr();
    }

    return sizeof(DoradBuf<YCSBTransaction>);
  }

  static int prefetch_cowns(const char* input)
  {
    auto txm = reinterpret_cast<const DoradBuf<YCSBTransaction>*>(input);

    for (int i = 0; i < ROWS_PER_TX; i++)
      __builtin_prefetch(reinterpret_cast<const void *>(
        txm->workload.cown_ptrs[i]), 1, 3);
    
    return sizeof(DoradBuf<YCSBTransaction>);
  }

  static int parse_and_process(const char* input)
  {
    auto txm = reinterpret_cast<const DoradBuf<YCSBTransaction>*>(input);

    auto pkt_addr = txm->pkt_addr;
    auto spin_usec = txm->workload.spin_usec;

    GET_COWN(0);GET_COWN(1);GET_COWN(2);GET_COWN(3);GET_COWN(4);
    GET_COWN(5);GET_COWN(6);GET_COWN(7);GET_COWN(8);GET_COWN(9);

    using AcqType = acquired_cown<YCSBRow>;
    when(row0,row1,row2,row3,row4,row5,row6,row7,row8,row9) << [pkt_addr, spin_usec]
      (AcqType acq_row0, AcqType acq_row1, AcqType acq_row2, AcqType acq_row3, 
       AcqType acq_row4, AcqType acq_row5, AcqType acq_row6, AcqType acq_row7,
       AcqType acq_row8, AcqType acq_row9)
    {
      /* std::cout << "hello inside when closure" << std::endl; */
      uint8_t sum = 0;
      int j;
      std::cout << "spin for " << spin_usec << std::endl;
      long spin_ns = static_cast<long>(spin_usec) * 1000;
      long next_ts = time_ns() + spin_ns; \
      while (time_ns() < next_ts) _mm_pause(); \
     /* TXN(0);TXN(1);TXN(2);TXN(3);TXN(4);TXN(5);TXN(6);TXN(7);TXN(8);TXN(9); */
      
      rte_mbuf* pkt = reinterpret_cast<rte_mbuf*>(pkt_addr);
      reply_pkt(pkt);
    };

    return sizeof(DoradBuf<YCSBTransaction>);
  }

  YCSBTransaction(const YCSBTransaction&) = delete;
  YCSBTransaction& operator=(const YCSBTransaction&) = delete;
};
  
YCSBTable* YCSBTransaction::table;

namespace YCSB {
int init()
{
  YCSBTransaction::table = new YCSBTable;

  uint8_t* cown_arr_addr = static_cast<uint8_t*>(aligned_alloc_hpage(
    1024 * DB_SIZE));

  for (uint64_t i = 0; i < DB_SIZE; i++)
  {
    cown_ptr<YCSBRow> cown_r = make_cown_custom<YCSBRow>(
        reinterpret_cast<void *>(cown_arr_addr + (uint64_t)1024 * i));

    YCSBTransaction::table->insert_row(i, cown_r);
  }

  return 0;
}
}
