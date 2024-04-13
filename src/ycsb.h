#include <thread>
#include <unordered_map>

#include "db.h" 
#include "dispatcher.h"
/* #include "txcounter.hpp" */
/* #include "constants.hpp" */
/* #include "pipeline.hpp" */
static constexpr uint32_t ROWS_PER_TX = 10;
static constexpr uint32_t ROW_SIZE = 900;
static constexpr uint32_t WRITE_SIZE = 100;


#define GET_COWN(_INDEX) auto&& row##_INDEX = get_cown_ptr_from_addr<YCSBRow>(reinterpret_cast<void *>(txm->workload.cown_ptrs[_INDEX]));
#define GET_ROW(_INDEX) auto&& row##_INDEX = index->get_row(txm->workload.indices[_INDEX]);
#define TXN(_INDEX) \
{ \
  if (write_set_l & 0x1) \
    memset(&acq_row##_INDEX->val, sum, WRITE_SIZE); \
  else \
  { \
    for (int j = 0; j < ROW_SIZE; j++) \
      sum += acq_row##_INDEX->val.payload[j]; \
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

struct YCSBRow
{
  char payload[ROW_SIZE];
};

class YCSBTransaction
{
public:
  static Index<YCSBRow>* index;

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
  }

  static int prepare_cowns(char* input)
  {
    auto txm = reinterpret_cast<DoradBuf<YCSBTransaction>*>(input);

    for (int i = 0; i < ROWS_PER_TX; i++)
    {
      auto&& cown = index->get_row(txm->workload.indices[i]);
      txm->workload.cown_ptrs[i] = cown.get_base_addr();
    }

    return sizeof(DoradBuf<YCSBTransaction>);
  }

  static int prefetch_cowns(const char* input)
  {
    auto txm = reinterpret_cast<const DoradBuf<YCSBTransaction>*>(input);

    for (int i = 0; i < ROWS_PER_TX; i++)
      __builtin_prefetch(reinterpret_cast<const void *>(
        txm->workload.cown_ptrs[i] + 32), 1, 3);
    
    return sizeof(DoradBuf<YCSBTransaction>);
  }

#ifdef RPC_LATENCY
  static int parse_and_process(const char* input, ts_type init_time)
#else
  static int parse_and_process(const char* input)
#endif // RPC_LATENCY
  {
    auto txm = reinterpret_cast<const DoradBuf<YCSBTransaction>*>(input);

    auto ws_cap = txm->workload.write_set;

#if defined(INDEXER) || defined(TEST_TWO)
    GET_COWN(0);GET_COWN(1);GET_COWN(2);GET_COWN(3);GET_COWN(4);
    GET_COWN(5);GET_COWN(6);GET_COWN(7);GET_COWN(8);GET_COWN(9);
#else
    GET_ROW(0);GET_ROW(1);GET_ROW(2);GET_ROW(3);GET_ROW(4);
    GET_ROW(5);GET_ROW(6);GET_ROW(7);GET_ROW(8);GET_ROW(9);   
#endif

    using AcqType = acquired_cown<YCSBRow>;
#ifdef RPC_LATENCY
    when(row0,row1,row2,row3,row4,row5,row6,row7,row8,row9) << [ws_cap, init_time]
#else
    when(row0,row1,row2,row3,row4,row5,row6,row7,row8,row9) << [ws_cap]
#endif
      (AcqType acq_row0, AcqType acq_row1, AcqType acq_row2, AcqType acq_row3, 
       AcqType acq_row4, AcqType acq_row5, AcqType acq_row6, AcqType acq_row7,
       AcqType acq_row8, AcqType acq_row9)
    {
      uint8_t sum = 0;
      uint16_t write_set_l = ws_cap;
      int j;
      /* TXN(0);TXN(1);TXN(2);TXN(3);TXN(4);TXN(5);TXN(6);TXN(7);TXN(8);TXN(9); */
      /* SPIN_RUN(); */
      /* M_LOG_LATENCY(); */
    };
    return sizeof(DoradBuf<YCSBTransaction>);
  }
  YCSBTransaction(const YCSBTransaction&) = delete;
  YCSBTransaction& operator=(const YCSBTransaction&) = delete;
};

Index<YCSBRow>* YCSBTransaction::index;

/* int main(int argc, char** argv) */
/* { */
/*   if (argc != 8 || strcmp(argv[1], "-n") != 0 || strcmp(argv[3], "-l") != 0) */
/*   { */
/*     fprintf(stderr, "Usage: ./program -n core_cnt -l look_ahead" */  
/*       " <dispatcher_input_file> -i <inter_arrival>\n"); */
/*     return -1; */
/*   } */

/*   uint8_t core_cnt = atoi(argv[2]); */
/*   uint8_t max_core = std::thread::hardware_concurrency(); */
/*   assert(1 < core_cnt && core_cnt <= max_core); */
  
/*   size_t look_ahead = atoi(argv[4]); */
/*   assert(8 <= look_ahead && look_ahead <= 128); */

/*   // Create rows (cowns) with huge pages and via static allocation */
/*   YCSBTransaction::index = new Index<YCSBRow>; */
/*   uint64_t cown_prev_addr = 0; */
/*   uint8_t* cown_arr_addr = static_cast<uint8_t*>(aligned_alloc_hpage( */
/*         1024 * DB_SIZE)); */

/*   for (int i = 0; i < DB_SIZE; i++) */
/*   { */
/*     cown_ptr<YCSBRow> cown_r = make_cown_custom<YCSBRow>( */
/*         reinterpret_cast<void *>(cown_arr_addr + (uint64_t)1024 * i)); */

/*     if (i > 0) */
/*       assert((cown_r.get_base_addr() - cown_prev_addr) == 1024); */
/*     cown_prev_addr = cown_r.get_base_addr(); */
    
/*     YCSBTransaction::index->insert_row(cown_r); */
/*   } */

/*   build_pipelines<YCSBTransaction>(core_cnt - 1, argv[5], argv[7]); */
/* } */
