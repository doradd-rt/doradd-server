#pragma once

#include <iostream>

class Dispatcher {
public:
  static int main(void *arg) {
    uint8_t q = reinterpret_cast<uint64_t>(arg);

    RTE_PER_LCORE(queue_id) = q;
    std::cout << "Hello from the dispatcher. Local queue " << q << std::endl;

    return 0;
  }
};
