CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
LDFLAGS ?=

PREFIX    ?= /usr/local
BINDIR    ?= $(PREFIX)/bin
LIBEXECDIR ?= $(PREFIX)/libexec
SBINDIR   ?= $(PREFIX)/sbin
CONFDIR   ?= /etc
SYSTEMDDIR ?= $(PREFIX)/lib/systemd/system

SRCDIR  = src
OBJDIR  = obj

CFLAGS  += -I$(SRCDIR)

DAEMON_LDFLAGS = $(LDFLAGS) -lpam -lX11
GREETER_LDFLAGS = $(LDFLAGS) -lX11

_CFLAGS  = $(CFLAGS)

DAEMON_SRC = \
	$(SRCDIR)/daemon.c \
	$(SRCDIR)/auth.c \
	$(SRCDIR)/session.c \
	$(SRCDIR)/strata.c \
	$(SRCDIR)/display.c \
	$(SRCDIR)/ipc_server.c \
	$(SRCDIR)/config.c \
	$(SRCDIR)/log.c \
	$(SRCDIR)/greeter.c

GREETER_SRC = \
	$(SRCDIR)/greeter_ui.c \
	$(SRCDIR)/ipc_client.c

DAEMON_OBJ = $(DAEMON_SRC:$(SRCDIR)/%.c=$(OBJDIR)/daemon_%.o)
GREETER_OBJ = $(GREETER_SRC:$(SRCDIR)/%.c=$(OBJDIR)/greeter_%.o)

.PHONY: all clean install uninstall

all: orbitd orbit-greeter

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/daemon_%.o: $(SRCDIR)/%.c $(SRCDIR)/orbit.h | $(OBJDIR)
	$(CC) $(_CFLAGS) -c -o $@ $<

$(OBJDIR)/greeter_%.o: $(SRCDIR)/%.c $(SRCDIR)/orbit.h | $(OBJDIR)
	$(CC) $(_CFLAGS) -c -o $@ $<

orbitd: $(DAEMON_OBJ)
	$(CC) $(_CFLAGS) -o $@ $^ $(DAEMON_LDFLAGS)

orbit-greeter: $(GREETER_OBJ)
	$(CC) $(_CFLAGS) -o $@ $^ $(GREETER_LDFLAGS)

install: all install-service
	install -d $(DESTDIR)$(SBINDIR)
	install -d $(DESTDIR)$(LIBEXECDIR)
	install -d $(DESTDIR)$(CONFDIR)
	install -d $(DESTDIR)$(CONFDIR)/pam.d
	install -m 755 orbitd $(DESTDIR)$(SBINDIR)/orbitd
	install -m 755 orbit-greeter $(DESTDIR)$(LIBEXECDIR)/orbit-greeter
	install -m 644 orbit-login.conf $(DESTDIR)$(CONFDIR)/orbit-login.conf
	ln -sf $(LIBEXECDIR)/orbit-greeter $(DESTDIR)$(BINDIR)/orbit-greeter
	@echo "Install PAM config: cp pam/orbit-login $(DESTDIR)$(CONFDIR)/pam.d/orbit-login"
	@echo "Enable with: sudo systemctl enable orbitd"

install-service:
	install -d $(DESTDIR)$(SYSTEMDDIR)
	install -m 644 orbitd.service $(DESTDIR)$(SYSTEMDDIR)/orbitd.service

uninstall:
	rm -f $(DESTDIR)$(SBINDIR)/orbitd
	rm -f $(DESTDIR)$(LIBEXECDIR)/orbit-greeter
	rm -f $(DESTDIR)$(BINDIR)/orbit-greeter
	rm -f $(DESTDIR)$(CONFDIR)/orbit-login.conf
	rm -f $(DESTDIR)$(SYSTEMDDIR)/orbitd.service

clean:
	rm -rf $(OBJDIR) orbitd orbit-greeter
