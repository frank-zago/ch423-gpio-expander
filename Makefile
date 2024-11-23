PWD         := $(shell pwd)
KVERSION    := $(shell uname -r)
KDIR ?= /lib/modules/$(KVERSION)/build

obj-m       := ch423.o

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
