# qingwei_mcp Driver - External Module Makefile
# Usage: make KDIR=/path/to/kernel/source [ARCH=arm64] [CROSS_COMPILE=aarch64-linux-gnu-]

KDIR ?= /path/to/kernel/source
ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

# For out-of-tree build
PWD := $(shell pwd)

# Default target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# For standalone Makefile auto-generation (used by ABK-KO build system)
.PHONY: all clean
