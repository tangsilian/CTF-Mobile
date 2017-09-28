obj-m := mem_driver.o

KDIR := /lib/modules/`uname -r`/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	rm -rf *.cmd *.o *.mod.c *.order *.symvers *.ko
