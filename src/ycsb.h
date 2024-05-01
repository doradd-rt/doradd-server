#include <thread>
#include <unordered_map>

#include "db.h" 
#include "non-deter/spinlock.h"
#include "dispatcher.h"
#include "generic_macros.hpp"

static constexpr uint32_t ROWS_PER_TX = 10;
static constexpr uint32_t ROW_SIZE = 900;
static constexpr uint32_t WRITE_SIZE = 100;
static constexpr uint64_t DB_SIZE = 10'000'000;
static constexpr long SPIN_TIME = 5'000;

class YCSBTable : public Table<uint64_t, spinlock*, DB_SIZE> {
public:
  YCSBTable() : Table<uint64_t, spinlock*, DB_SIZE> () {
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

#define GET_COWN_ADDR(i, _) uint32_t a##i = txm->workload.indices[i];
#define GET_COWN(j, _) \
  { \
  spinlock* p##j = table->get_row(vec[j]); \
  lock_vec.push_back(p##j); \
}

#define SPIN_RUN() \
  { \
    long next_ts = time_ns() + SPIN_TIME; \
    while (time_ns() < next_ts) _mm_pause(); \
  } \

#define PUSH_VEC(i, _) vec.push_back(a##i);

#define VEC_INIT() \
    std::vector<uint64_t> vec; \
    std::vector<spinlock*> lock_vec; \
    vec.reserve(ROWS_PER_TX); lock_vec.reserve(ROWS_PER_TX); \
    std::sort(vec.begin(), vec.end()); \

#define VEC_SORT() std::sort(vec.begin(), vec.end());
#define BODY_START() for (int j = 0; j < ROWS_PER_TX; j++) { lock_vec[j]->lock(); }
#define BODY_END() for (int j = ROWS_PER_TX - 1; j >= 0; j--) { lock_vec[j]->unlock(); }

class YCSBTransaction
{
public:
  static YCSBTable* table;

  struct __attribute__((packed)) Marshalled
  {
    uint32_t indices[ROWS_PER_TX];
    uint16_t write_set;
    uint64_t lown_ptrs[ROWS_PER_TX];
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

    txm->workload.write_set = payload->write_set; 
  }

  static int prepare_cowns(char* input)
  {
    auto txm = reinterpret_cast<DoradBuf<YCSBTransaction>*>(input);

    for (int i = 0; i < ROWS_PER_TX; i++)
    {
      auto&& lown = table->get_row(txm->workload.indices[i]);
      //txm->workload.lown_ptrs[i] = lown.get_base_addr();
    }

    return sizeof(DoradBuf<YCSBTransaction>);
  }

  static int prefetch_cowns(const char* input)
  {
    auto txm = reinterpret_cast<const DoradBuf<YCSBTransaction>*>(input);

    //for (int i = 0; i < ROWS_PER_TX; i++)
    //  __builtin_prefetch(reinterpret_cast<const void *>(
    //    txm->workload.lown_ptrs[i]), 1, 3);
    
    return sizeof(DoradBuf<YCSBTransaction>);
  }

  static int parse_and_process(const char* input)
  {
    auto txm = reinterpret_cast<const DoradBuf<YCSBTransaction>*>(input);

    auto ws_cap = txm->workload.write_set;
    auto pkt_addr = txm->pkt_addr;

    EVAL(REPEAT(10, GET_COWN_ADDR, ~))
    when() << [ws_cap, pkt_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9]()
    {
      VEC_INIT(); EVAL(REPEAT(10, PUSH_VEC, ~)) VEC_SORT();
      EVAL(REPEAT(10, GET_COWN, ~))
      BODY_START();
      uint8_t sum = 0;
      uint16_t write_set_l = ws_cap;
      int j;
      SPIN_RUN();
      BODY_END();
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

  for (uint64_t i = 0; i < DB_SIZE; i++)
  {
    spinlock* splock_ptr = new spinlock;
    YCSBTransaction::table->insert_row(i, splock_ptr);
  }

  return 0;
}
}
