.PHONY: build

ROOTDIR=$(shell git rev-parse --show-toplevel)

dpdk:
	cd external/dpdk && meson --prefix $(ROOTDIR)/build/dpdk-install-dir -Dplatform=generic build && cd build && ninja && ninja install

