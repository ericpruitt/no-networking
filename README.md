no-networking
=============

This repository provides a tool for limiting a process's network access on
Linux using iptables that match connections based on supplementary groups.
After the package has been installed, the command "no-networking" can be used
to limit what a process can connect to.

Usage
-----

### Options ###

**Synopsis:** `no-networking [-lnp] COMMAND [ARGUMENT]...`

#### -l ####

Allow connections to loopback addresses 127.0.0.0/8 and ::1.

#### -n ####

Forbid all connections. This is the default behavior, but this option can be
used to override previously specified flags.

#### -p ####

Allow connections to private addresses — loopback addresses, 10.0.0.0/8,
169.254.0.0/16, 172.16.0.0/12, 192.168.0.0/16, 198.18.0.0/15, fc00::/7 and
fe80::/10.

### Groups ###

There are through groups the tool manages:

- **no-networking:** Members of this group cannot make any network connections.
- **loopback-networking:** Members of this group can make connections to
  loopback addresses — 127.0.0.0/8 and ::1.
- **private-networking:** Members of this group can make connections to any
  private address — 127.0.0.0/8, ::1, 10.0.0.0/8, 172.16.0.0/12,
  192.168.0.0/16, 169.254.0.0/16, fc00::/7 and fe80::/10.

The no-networking CLI only applies the restrictions to processes that it spawns
which it does by temporarily amending a user's groups with _setgroups(2)_, but
the restrictions can be made to apply to all of a user's processes by adding
the user to a particular group. For example, `sudo useradd -G no-networking
wine` would prevent the "wine" user from making any network connections. If
multiple groups are applied, the most limited group's restrictions take
precedence. If a user is a member of multiple groups, the iptables rules are
coded so the most restrictive group takes precedence.

Development
-----------

The no-networking C code only makes use of standard GNU libc functions and
requires nothing other than a C99 compiler to build, but
[fakeroot](https://wiki.debian.org/FakeRoot) is required to create the Debian
package. The script "util.sh" is responsible for creating groups, and creating
and deleting iptables rules those controls what those groups can access. It
also generates a header file named "groups.h" that no-networking.c uses as its
source of truth for group names. Since no-networking works using supplemental
groups matched by iptables rules, the binary is installed with the setuid bit.

**Makefile Targets:**
- **package:** Build a Debian package that provides the no-networking CLI. This
  is the default target.
- **clean:** Clean up build artifacts.
- **lint:** If installed, use Clang and/or GCC for static analysis.
- **test:** Do some basic tests to verify that no-networking package is working
  as expected. This test must be run after installing the package. This target
  relies on _man(1)_, _ip(8)_ and _ping(8)_.
