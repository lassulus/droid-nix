CC ?= cc
# Non-PIE, static, fixed-address: the syscall stub must sit at the same
# address in every process so one seccomp filter can exempt it across execve.
CFLAGS ?= -O2 -Wall -static -no-pie -fno-pie
PREFIX ?= /usr/local

loader: loader.c stub.S
	$(CC) $(CFLAGS) -o $@ loader.c stub.S

install: loader
	install -D -m 0755 -t $(PREFIX)/bin loader

clean:
	rm -f loader
