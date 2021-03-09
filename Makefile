kunetik-objs := kunetik_core.o
obj-m := kunetik.o

### uncomment line below to enable debug output ###
# ccflags-y := -DDEBUG 

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install: all
	insmod kunetik.ko
	sleep 0.4
	chmod 666 /dev/kunetik
	
### The below only works with a signed module ###
#install: all
#	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
