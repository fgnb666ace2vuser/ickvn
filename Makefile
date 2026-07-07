obj-m += hw_drv.o
KDIR := $(KERNEL_DIR)
PWD := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) modules ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

clean:
	make -C $(KDIR) M=$(PWD) clean