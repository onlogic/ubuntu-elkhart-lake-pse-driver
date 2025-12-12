obj-m += pse.o

KERNELRELEASE ?= `uname -r`
KERNEL_DIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C ${KERNEL_DIR} M=$(PWD) modules

clean:
	$(MAKE) -C ${KERNEL_DIR} M=$(PWD) clean
