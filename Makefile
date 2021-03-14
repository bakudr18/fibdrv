CONFIG_MODULE_SIG = n
TARGET_MODULE := fibdrv
TARGETS := client utime ktime
CFLAGS := -Wall

obj-m := $(TARGET_MODULE).o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

GIT_HOOKS := .git/hooks/applied

.PHONY: all clean $(TARGETS)

all: $(GIT_HOOKS) client utime ktime
	$(MAKE) -C $(KDIR) M=$(PWD) modules

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) out $(TARGETS) time_elapsed.png scripts/utime.txt scripts/ktime.txt
load:
	sudo insmod $(TARGET_MODULE).ko
unload:
	sudo rmmod $(TARGET_MODULE) || true >/dev/null

utime: utime.o mlock_check.o
	$(CC) $(CFLAGS) -o $@ $^

ktime: ktime.c
	$(CC) $(CFLAGS) -o $@ $^

client: client.c
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

performance:
	$(MAKE) all
	$(MAKE) load
	sudo taskset -c 7 ./utime > scripts/utime.txt
	sudo taskset -c 7 ./ktime > scripts/ktime.txt
	$(MAKE) unload
	gnuplot scripts/time_elapsed.gp
	eog time_elapsed.png

PRINTF = env printf
PASS_COLOR = \e[32;01m
NO_COLOR = \e[0m
pass = $(PRINTF) "$(PASS_COLOR)$1 Passed [-]$(NO_COLOR)\n"

check: all
	$(MAKE) unload
	$(MAKE) load
	sudo ./client > out
	$(MAKE) unload
	@diff -u out scripts/expected.txt && $(call pass)
	@scripts/verify.py
