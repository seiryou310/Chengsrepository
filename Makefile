ifneq($(KERNELRELEASE),)
	obj-m += globalmem.o
else
	KERNELRELEASE ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif
