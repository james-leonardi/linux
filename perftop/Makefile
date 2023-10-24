obj-m := perftop.o
CONFIG_MODULE_SIG=n
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: perftop.c
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) modules

install: all
	insmod perftop.ko

uninstall:
	rmmod perftop

reinstall: uninstall install
