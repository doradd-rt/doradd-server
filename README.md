# DORADD-Server

This repo extends the [DORADD](https://github.com/doradd-rt/doradd) with a DPDK networking stack. We plan to merge this into the main repo as soon as possible. 

---

## Setup

**Network configuration - MAC address and Port ID**

Currently, before building the source code, one need to specify the MAC address and DPDK port ID. To find out the available DPDK port, you should use `dpdk/usertools/devbind.py -s` to check the port status and its DPDK port ID.

For instance, if the interface mac address is `1c:34:da:41:ca:bc`, given we manually set the server IP address is `<subnet>.2` (in [src/config.h](https://github.com/doradd-rt/doradd-server/blob/ccbe44f8a0b31091b81909a7fe9f6caf726ebd38/src/config.h#L8)), you need to update the 2nd entry in [src/arp-cfg.h](https://github.com/doradd-rt/doradd-server/blob/main/src/arp-cfg.h) as server mac address `1c:34:da:41:ca:bc` , based our pre-set L2 forwarding rule. Similarly, you need to update the client mac address accordingly in the arp entries. If the client IP is `<subnet>.3` , then you should update the 3rd entry with the client mac address.

## Build

```bash
git submodule update --init
make dpdk
cd scripts && sudo ./hugepages.sh
cd ../src && mkdir build && cd build
cmake .. -GNinja -DCMAKE_BUILD_TYPE=Release
```

## Run

```c
sudo taskset -c 1-12 ./server
```

Note: the last four cores are set as dispatcher pipelines, and the rest are worker cores.
