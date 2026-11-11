# Out-of-tree build of the patched hid-magicmouse. DKMS runs this for every
# kernel; `make` by hand builds for the running one.
ifneq ($(KERNELRELEASE),)
obj-m := hid-magicmouse.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

# linux-aurora is built with clang. Use it when it is installed, as the
# kernel's own build would, and gcc otherwise. DKMS passes LLVM=1 for such a
# kernel whether clang exists or not, so the choice is spelled out here.
ifeq ($(shell grep -qs CONFIG_CC_IS_CLANG=y $(KDIR)/.config && command -v clang >/dev/null 2>&1 && command -v ld.lld >/dev/null 2>&1 && echo y),y)
TOOLCHAIN := LLVM=1
else
TOOLCHAIN := LLVM=
endif

all: check
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(TOOLCHAIN) modules

# The module replaces the kernel's own driver, so it is only built for a
# kernel whose driver still looks like the one the patch was made from.
check:
	sh $(CURDIR)/abi-check.sh $(KDIR)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $(TOOLCHAIN) clean

.PHONY: all check clean
endif
