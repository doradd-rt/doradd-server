#pragma once

#include <unordered_map>
#include <optional>
#include <cpp/when.h>

using namespace verona::rt;
using namespace verona::cpp;

template<typename Key, typename T, uint64_t DB_SIZE>
struct Table
{
private:
  std::array<cown_ptr<T>, DB_SIZE> map;
  uint64_t cnt = 0;

public:
  void * start_addr = 0;

  Table() : map(std::array<cown_ptr<T>, DB_SIZE>()) {}

  cown_ptr<T>* get_row_addr(uint64_t key)
  {
    return &map[key];
  }

  cown_ptr<T>&& get_row(uint64_t key)
  {
    return std::move(map[key]);
  }

  uint64_t insert_row(Key key, cown_ptr<T> r)
  {
    if (cnt < DB_SIZE)
    {
      map[key] = r;
      return cnt++;
    }
    else
    {
      printf("DB is %lu\n", DB_SIZE);
      throw std::out_of_range("Index is full");
    }
  }
};
