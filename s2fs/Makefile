obj-m := s2fs.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: s2fs.c
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	insmod s2fs.ko

uninstall:
	rmmod s2fs

reinstall: uninstall install

mount: all install
	mkdir -p mnt
	mount -t s2fs nodev ./mnt

umount:
	umount ./mnt
	rm -rf ./mnt
	rmmod s2fs

remount: umount mount
