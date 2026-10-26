CC ?= cc
# Non-PIE, static, fixed-address: the syscall stub must sit at the same
# address in every process so one seccomp filter can exempt it across execve.
# Linked at 64 GiB, far above where non-PIE (ET_EXEC) targets live (aarch64
# default 0x400000) and below the mmap area of a 39-bit address space, so the
# loader can map ET_EXEC targets at their own link addresses.
CFLAGS ?= -O2 -Wall -static -no-pie -fno-pie
LDFLAGS ?= -Wl,-Ttext-segment=0x1000000000
PREFIX ?= /usr/local

loader: loader.c stub.S
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ loader.c stub.S

install: loader
	install -D -m 0755 -t $(PREFIX)/bin loader

clean:
	rm -f loader
