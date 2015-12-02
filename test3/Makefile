obj-m = dm-cache.o 
all: 
	make -C /usr/src/linux-source-3.2/build M=$(PWD) modules
clean: 
	rm -rf *.o *.ko *.mod.c *.mod.o modules.* Mod*
