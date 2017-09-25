obj-m    := veth.o


veth-y	:= v.o \
		../common/dysco_main.o ../common/dysco_input.o ../common/dysco_output.o \
		../common/dysco_control_output.o ../common/dysco_control_input.o \
		../common/dysco_proc.o ../common/dysco_tcp.o


CXX = gcc 
KDIR    := /lib/modules/$(shell uname -r)/build
PWD    := $(shell pwd)


default: 
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules  
clean: cleanuser
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean

cleanuser:
	cd ../common && $(MAKE) clean
	rm -rf *.o
