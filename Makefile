KERNEL_VERSION := $(shell uname -r)
MAKE_CMD := make -C /lib/modules/$(KERNEL_VERSION)/build M=$(PWD)
obj-m += tlm.o

all: module
clean: module_clean

distclean: clean
	rm -f *.ko

module:
	$(MAKE_CMD) modules

module_clean:
	$(MAKE_CMD) clean

.PHONY: distclean clean module_clean
