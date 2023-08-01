obj-m += gigabyte-laptop.o

all: modules

modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod gigabyte-laptop.ko

unload:
	-rmmod gigabyte-laptop

reload: unload load
