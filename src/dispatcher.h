#pragma once

#include <iostream>

#include <dpdk.h>

extern "C" {
#include <rte_flow.h>
}

class Dispatcher {
  void configure_rx_queue() {
    // Receive all packets in the dispatcher queue
    int ret;
    struct rte_flow *f;

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

    queue.index = 0; // RTE_PER_LCORE(queue_id);
    actions[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    actions[0].conf = &queue;
    actions[1].type = RTE_FLOW_ACTION_TYPE_END;

    ret = rte_flow_validate(0, &attr, pattern, actions, &err);
    if (ret) {
      std::cout << "Error creating flow : " << err.message << std::endl;
      return;
    }
    f = rte_flow_create(0, &attr, pattern, actions, &err);
    assert(f);
  }

public:
  void main() {
    configure_rx_queue();

    while (1) {
      DPDKManager::dpdk_poll();
    }

    std::cout << "Configured the flow director without a problem\n";
  }

  static int main(void *arg) {
    uint8_t q = reinterpret_cast<uint64_t>(arg);

    RTE_PER_LCORE(queue_id) = q;
    std::cout << "Hello from the dispatcher. Local queue " << q << std::endl;

    Dispatcher d;

    d.main();

    return 0;
  }
};
