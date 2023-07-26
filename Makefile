obj-m += gigabyte-ec.o

all: modules

modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod gigabyte-ec.ko

unload:
	-rmmod gigabyte-ec

reload: unload load
