obj-m := enishifs.o

enishifs-y := main.o \
              inode.o \
              musubi.o

KDIR ?= ../kernel

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
