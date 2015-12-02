obj-m = dm-cache.o 
all: 
	make -C /lib/modules/3.2.73/build M=$(PWD) modules
clean: 
	rm -rf *.o *.ko *.mod.c *.mod.o modules.* Mod*
