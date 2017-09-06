obj-m := cat3626.o 
KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf Module.symvers modules.order *.mod.o *.o *.ko *.cmd *.mod.c .tmp_versions
