obj-m := s2fs.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: s2fs.c
	make -C $(KDIR) M=$(PWD) modules

clean:
	rm -f modules.order .modules.order.cmd Module.symvers .Module.symvers.cmd s2fs.ko .s2fs.ko.cmd s2fs.mod s2fs.mod.c .s2fs.mod.cmd s2fs.mod.o .s2fs.mod.o.cmd s2fs.o .s2fs.o.cmd

install: all
	insmod s2fs.ko

uninstall:
	rmmod s2fs

reinstall: uninstall install
