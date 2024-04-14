#include <thread>
#include <unordered_map>

#include "db.h" 
#include "dispatcher.h"
/* #include "pipeline.hpp" */
static constexpr uint32_t ROWS_PER_TX = 10;
static constexpr uint32_t ROW_SIZE = 900;
static constexpr uint32_t WRITE_SIZE = 100;
static constexpr uint64_t DB_SIZE = 100;

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
/* #define M_LOG_LATENCY() \ */
/*   { \ */
/*     if constexpr (LOG_LATENCY) { \ */
/*       auto time_now = std::chrono::system_clock::now(); \ */
/*       std::chrono::duration<double> duration = time_now - init_time; \ */
/*       uint32_t log_duration = static_cast<uint32_t>(duration.count() * 1'000'000); \ */
/*       TxCounter::instance().log_latency(log_duration); \ */
/*     } \ */
/*     TxCounter::instance().incr(); \ */
/*   } */
/* #define SPIN_RUN() \ */
/*   { \ */
/*     long next_ts = time_ns() + 100'000; \ */
/*     while (time_ns() < next_ts) _mm_pause(); \ */
/*   } \ */

class YCSBTransaction
{
public:
  static YCSBTable* table;

  struct __attribute__((packed)) Marshalled
  {
    uint32_t indices[ROWS_PER_TX];
    uint16_t write_set;
    uint64_t cown_ptrs[ROWS_PER_TX];
    uint8_t  pad[6];
  };
  static_assert(sizeof(Marshalled) == 128);
 
  static void parse_pkt(uint64_t pkt_addr)
  {
    auto pkt = reinterpret_cast<rte_mbuf*>(pkt_addr);
    
    // FIXME: add more logic here to populate the txns
  }

  static int prepare_cowns(char* input)
  {
    auto txm = reinterpret_cast<DoradBuf<YCSBTransaction>*>(input);

    for (int i = 0; i < ROWS_PER_TX; i++)
    {
      //auto&& cown = table->get_row(txm->workload.indices[i]);
      auto&& cown = table->get_row(i); // FIXME: hardcode for testing here
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

    auto ws_cap = txm->workload.write_set;
    auto pkt_addr = txm->pkt_addr;

    GET_COWN(0);GET_COWN(1);GET_COWN(2);GET_COWN(3);GET_COWN(4);
    GET_COWN(5);GET_COWN(6);GET_COWN(7);GET_COWN(8);GET_COWN(9);

    using AcqType = acquired_cown<YCSBRow>;
    when(row0,row1,row2,row3,row4,row5,row6,row7,row8,row9) << [ws_cap, pkt_addr]
      (AcqType acq_row0, AcqType acq_row1, AcqType acq_row2, AcqType acq_row3, 
       AcqType acq_row4, AcqType acq_row5, AcqType acq_row6, AcqType acq_row7,
       AcqType acq_row8, AcqType acq_row9)
    {
      uint8_t sum = 0;
      uint16_t write_set_l = ws_cap;
      int j;
      TXN(0);TXN(1);TXN(2);TXN(3);TXN(4);TXN(5);TXN(6);TXN(7);TXN(8);TXN(9);
      /* SPIN_RUN(); */
      /* M_LOG_LATENCY(); */
      
      rte_mbuf* pkt = reinterpret_cast<rte_mbuf*>(pkt_addr);
      reply_pkt(pkt);
    };
    return sizeof(DoradBuf<YCSBTransaction>);
  }
  YCSBTransaction(const YCSBTransaction&) = delete;
  YCSBTransaction& operator=(const YCSBTransaction&) = delete;
};

YCSBTable* YCSBTransaction::table;
