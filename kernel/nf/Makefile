obj-m    := dysco.o


dysco-y	:= d.o \
		../common/dysco_main.o ../common/dysco_input.o ../common/dysco_output.o \
		../common/dysco_control_output.o ../common/dysco_control_input.o \
		../common/dysco_proc.o ../common/dysco_tcp.o

CXX = gcc 
KDIR    := /lib/modules/$(shell uname -r)/build
PWD    := $(shell pwd)
# 	$(MAKE) $(CF) -C $(KDIR) SUBDIRS=$(PWD) modules  
# 	KCPPFLAGS="-DDYSCO_MEASUREMENT_INPUT_REWRITE" $(MAKE) $(CF) -C $(KDIR) SUBDIRS=$(PWD) modules
#	KCPPFLAGS="-DDYSCO_MEASUREMENT_OUTPUT_REWRITE" $(MAKE) $(CF) -C $(KDIR) SUBDIRS=$(PWD) modules

default: 
	KCPPFLAGS="-DDYSCO_MEASUREMENT_OUTPUT_TRANSLATE" $(MAKE) $(CF) -C $(KDIR) SUBDIRS=$(PWD) modules

clean: cleanuser
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean
	cd ../common && $(MAKE) clean
cleanuser:
	rm -rf *.o
