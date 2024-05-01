#pragma once

#include <optional>

#include "hugepage.h"

template<typename Key, typename T, uint64_t DB_SIZE>
struct Table
{
private:
  std::array<T, DB_SIZE> map;
  uint64_t cnt = 0;

public:
  Table() : map(std::array<T, DB_SIZE>()) {}

  T* get_row_addr(uint64_t key)
  {
    return &map[key];
  }

  T&& get_row(uint64_t key)
  {
    return std::move(map[key]);
  }

  uint64_t insert_row(Key key, T r)
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
