obj-m += hw_drv.o
KDIR ?= $(PWD)/kernel
PWD := $(shell pwd)

all:
	MAKEFLAGS="$(MAKEFLAGS)" make -C $(KDIR) M=$(PWD) modules ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KBUILD_EXTRA_SYMBOLS="$(KDIR)/Module.symvers" MODFLAGS="-fno-pic"

clean:
	make -C $(KDIR) M=$(PWD) clean