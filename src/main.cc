#include <cpp/when.h>
#include <iostream>
#include <verona.h>

using namespace verona::rt;
using namespace verona::cpp;

int main(int argc, char **argv) {
  uint8_t nr_cpu = atoi(argv[1]);

  auto &sched = Scheduler::get();
  Scheduler::set_detect_leaks(true);
  sched.set_fair(true);
  sched.init(nr_cpu);

  when() << [] { std::cout << "hello behaviours\n"; };
  when() << [] {
    while (1)
      ;
  };

  sched.run();
}
