CC ?= cc
CFLAGS ?= -O2 -Wall -static
PREFIX ?= /usr/local
PROGS = probe_sud probe_seccomp probe_mmap0 probe_interp bench

all: $(PROGS)

probe_sud probe_seccomp bench: %: %.c common.h stub.S
	$(CC) $(CFLAGS) -o $@ $< stub.S

probe_mmap0 probe_interp: %: %.c
	$(CC) $(CFLAGS) -o $@ $<

install: all
	install -D -m 0755 -t $(PREFIX)/bin $(PROGS)
