.POSIX:
.SILENT: groups.h lint test test-routing test-man-page-readable

CC ?= c99
CPPFLAGS ?= -D_DEFAULT_SOURCE
CFLAGS ?= -lc

DEB_TARGETS = \
	deb/DEBIAN/control \
	deb/DEBIAN/postrm \
	deb/DEBIAN/preinst \
	deb/usr/bin/no-networking \
	deb/usr/share/man/man1/no-networking.1 \

# Used to ensure ping fails or succeeds quickly. Since we always ping loopback
# addresses, 5ms should be more than enough time to get a response.
PING = ping -W 0.005 -c 1

package:
	umask 0022 && fakeroot $(MAKE) no-networking.deb

deb/DEBIAN/control: control
	install -m 644 -D $? $@.tmp
	architecture="$$(dpkg --print-architecture)"; \
	sed "s/Architecture:.*/Architecture: $$architecture/" $@.tmp > $@
	rm $@.tmp

deb/DEBIAN/util.sh: util.sh
	install -m 755 -D $? $@

deb/DEBIAN/preinst deb/DEBIAN/postrm: deb/DEBIAN/util.sh
	install -m 755 -D $? $@

groups.h: util.sh
	./$? > $@.tmp
	mv $@.tmp $@

no-networking: no-networking.c groups.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $@.c -o $@

lint: no-networking.c groups.h
	if (! command -v clang && ! command -v gcc) >/dev/null; then \
		echo "$@: neither Clang nor GCC is installed"; \
		exit 1; \
	fi
	if command -v clang >/dev/null; then \
		clang -std=c99 $(CPPFLAGS) $(CFLAGS) -Werror \
			-Weverything -fsyntax-only no-networking.c \
		|| trap "exit 1" EXIT; \
	fi; \
	if command -v gcc >/dev/null; then \
		gcc -std=c99 $(CPPFLAGS) $(CFLAGS) -Werror \
			-Wall -Wpedantic -fsyntax-only no-networking.c \
		|| trap "exit 1" EXIT; \
	fi

deb/usr/bin/no-networking: no-networking
	install -D -m 4755 $? $@

deb/usr/share/man/man1/no-networking.1: no-networking.1
	mkdir -p $(@D)
	if git diff --quiet; then \
		date="$$( \
			date --date="@$$(git log -1 --format="%ct")" "+%B %Y" \
		)"; \
	else \
		date="$$(date "+%B %Y")"; \
	fi; \
	version="$$(grep ^Version: control | cut -d ' ' -f 2)"; \
	sed -e "s/%DATE%/$$date/" -e "s/%VERSION%/$$version/" $? > $@

no-networking.deb: $(DEB_TARGETS)
	dpkg -b deb $@

test-routing:
	test -z "$$(ip -4 addr show lo)" || ips="$$ips 127.0.0.1"; \
	test -z "$$(ip -6 addr show lo)" || ips="$$ips ::1"; \
	if [ -z "$$ips" ]; then \
		echo "$@: no loopback interfaces found"; \
		exit 1; \
	fi; \
	for ip in $$ips; do \
		if ! $(PING) "$$ip" >/dev/null; then \
			echo "$@: $$ip: address unreachable; cannot test"; \
			trap "exit 1" EXIT; \
			continue; \
		fi; \
		if no-networking $(PING) "$$ip" >/dev/null; then \
			echo "$@: $$ip: still reachable with no-networking"; \
			trap "exit 1" EXIT; \
		fi; \
		if ! no-networking -l $(PING) "$$ip" >/dev/null; then \
			echo "$@: $$ip: not reachable with \"-l\""; \
			trap "exit 1" EXIT; \
		fi; \
		if ! no-networking -p $(PING) "$$ip" >/dev/null; then \
			echo "$@: $$ip: not reachable with \"-p\""; \
			trap "exit 1" EXIT; \
		fi; \
	done
	echo "$@: OK"

test-man-page-readable:
	if ! man no-networking >/dev/null 2>&1; then \
		echo "$@: cannot load man page"; \
		exit 1; \
	fi
	echo "$@: OK"

test: test-routing test-man-page-readable

clean:
	rm -rf groups.h deb/ *.deb no-networking
