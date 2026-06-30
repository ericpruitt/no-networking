/**
 * This tool adds supplemental groups to restrict network access of the process
 * it spawns.
 *
 * - Make: c99 -D_DEFAULT_SOURCE -lc no-networking.c -o no-networking
 */
#include <arpa/inet.h>
#include <errno.h>
#include <grp.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "groups.h"

#define EXIT_COMMAND_NOT_FOUND 127
#define EXIT_EXEC_FAILURE 126

/**
 * These values are used to represent the different types of restrictions.
 */
typedef enum {
    /**
     * Block all network access.
     */
    ALLOW_NONE,
    /**
     * Allow connections to loopback addresses.
     */
    ALLOW_LOOPBACK,
    /**
     * Allow connections to private addresses.
     */
    ALLOW_PRIVATE_NETWORKS,
} allow_mode_et;

/**
 * Destination port used for connection probing packets. The port is mostly
 * arbitrary, but 53 was selected since DNS is rarely restricted by firewalls.
 */
#define REMOTE_PROBE_PORT 53

/**
 * These IP addresses are probed via UDP to help verify that the desired
 * network restrictions are active.
 */
const char **probe_dests[] = {
    [ALLOW_NONE] = (const char *[]) {
        "198.41.0.4",           // a.root-servers.net (IPv4)
        "2001:503:ba3e::2:30",  // a.root-servers.net (IPv6)
        "10.0.0.0",
        "169.254.0.0",
        "172.16.0.0",
        "192.168.0.0",
        "198.18.0.0",
        "127.0.0.0",
        "::1",
        NULL,
    },
    [ALLOW_LOOPBACK] = (const char *[]) {
        "198.41.0.4",
        "2001:503:ba3e::2:30",
        "10.0.0.0",
        "169.254.0.0",
        "172.16.0.0",
        "192.168.0.0",
        "198.18.0.0",
        NULL,
    },
    [ALLOW_PRIVATE_NETWORKS] = (const char *[]) {
        "198.41.0.4",
        "2001:503:ba3e::2:30",
        NULL,
    },
};

char *argv0 = "no-networking";
allow_mode_et mode = ALLOW_NONE;

/**
 * Like _printf(3)_, but prints to standard error instead of standard output.
 *
 * Arguments: See _printf(3)_.
 *
 * Return: See _printf(3)_.
 */
#define eprintf(...) fprintf(stderr, __VA_ARGS__)

/**
 * Probe various IPv4 addresses to try to verify that the desired network
 * restrictions are in place.
 *
 * Return: 0 if the system is not configured for IPv4 or there were no
 * anomalies discovered by the probing process. Otherwise, -1 is returned.
 */
static int probe_ipv4(void)
{
    int sockfd;
    struct sockaddr_in dest;

    int result = 0;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        if (errno == EAFNOSUPPORT) {
            return 0;
        }

        perror("IPv4 UDP socket creation failed");
        return -1;
    }

    // We set the TTL to 1 to try to minimize information leakage when probing
    // a public address.
    int ttl = 1;

    if (setsockopt(sockfd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) == -1) {
        perror("unable to set TTL for IPv4 UDP socket");
        close(sockfd);
        return -1;
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(REMOTE_PROBE_PORT);

    for (const char **cursor = probe_dests[mode]; *cursor; cursor++) {
        // Ignore IPv6 addresses.
        if (strchr(*cursor, ':')) {
            continue;
        }

        dest.sin_addr.s_addr = inet_addr(*cursor);
        ssize_t sent = sendto(
           sockfd, NULL, 0, 0, (const struct sockaddr *) &dest, sizeof(dest)
        );

        if (sent != -1) {
            eprintf("%s: probing %s unexpectedly succeeded\n", argv0, *cursor);
            result = -1;
        }
    }

    close(sockfd);
    return result;
}

/**
 * Probe various IPv6 addresses to try to verify that the desired network
 * restrictions are in place.
 *
 * Return: 0 if the system is not configured for IPv6 or there were no
 * anomalies discovered by the probing process. Otherwise, -1 is returned.
 */
static int probe_ipv6(void)
{
    const char *host;
    int sockfd;
    struct sockaddr_in6 dest;

    int result = 0;
    char buf[64] = "::ffff:";
    char *eob = buf + strlen(buf);

    if ((sockfd = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
        if (errno == EAFNOSUPPORT) {
            return 0;
        }

        perror("IPv6 UDP socket creation failed");
        return -1;
    }

    // We set the TTL to 1 to try to minimize information leakage when probing
    // a public address.
    int ttl = 1;

    if (
      setsockopt(sockfd, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl))) {
        perror("unable to set TTL for IPv6 UDP socket");
        close(sockfd);
        return -1;
    }

    memset(&dest, 0, sizeof(dest));
    dest.sin6_family = AF_INET6;
    dest.sin6_port = htons(REMOTE_PROBE_PORT);

    for (const char **cursor = probe_dests[mode]; *cursor; cursor++) {
        // If the address is an IPv4 address, we need to map it to IPv6.
        if (strchr(*cursor, ':')) {
            host = *cursor;
        } else {
            strcpy(eob, *cursor);
            host = (const char *) buf;
        }

        if (inet_pton(AF_INET6, host, &dest.sin6_addr) <= 0) {
            perror(*cursor);
            result = -1;
            continue;
        }

        ssize_t sent = sendto(
            sockfd, NULL, 0, 0, (const struct sockaddr *) &dest, sizeof(dest)
        );

        if (sent != -1) {
            eprintf("%s: probing %s unexpectedly succeeded\n", argv0, host);
            result = -1;
        }
    }

    close(sockfd);
    return result;
}

static void usage(void)
{
    printf("Usage: %s [-dlnp] COMMAND [ARGUMENT]...\n", argv0);
    printf("       %s -h\n", argv0);
    printf("       %s -V\n", argv0);
    printf("       %s --help\n", argv0);
    printf("\n");
    printf("Options:\n");
    printf("  -h, -V, --help\n");
    printf("        Show this documentation and exit.\n");
    printf("  -d    Disable UDP connection probing.\n");
    printf("  -l    Allow connections to loopback addresses.\n");
    printf("  -n    Forbid all connections. This is the default behavior.\n");
    printf("  -p    Allow connections to private addresses.\n");
}

int main(int argc, char **argv)
{
    const char *group;
    size_t group_count;
    struct group *group_fields;
    gid_t *groups;
    int option;
    long result;
    size_t sc_ngroups_max;

    gid_t egid = getegid();
    gid_t gid = getgid();
    uid_t euid = geteuid();
    uid_t uid = getuid();

    bool udp_probing_enabled = true;

    const char *restriction_groups[] = {
        [ALLOW_NONE] = NO_NETWORKING_GROUP,
        [ALLOW_LOOPBACK] = LOOPBACK_NETWORKING_GROUP,
        [ALLOW_PRIVATE_NETWORKS] = PRIVATE_NETWORKING_GROUP,
    };

    if (argc > 0) {
        argv0 = basename(argv[0]);
    }

    if (argc > 1) {
        for (char **arg = argv; *arg; arg++) {
            if (strcmp(*arg, "--") == 0) {
                break;
            } else if (strcmp(*arg, "--help") == 0) {
                usage();
                return EXIT_SUCCESS;
            }
        }
    }

    while ((option = getopt(argc, argv, "+dhlnpV")) != -1) {
        switch (option) {
          case 'd':
            udp_probing_enabled = false;
            break;

          case 'h':
          case 'V':
            usage();
            return EXIT_SUCCESS;

          case 'l':
            mode = ALLOW_LOOPBACK;
            break;

          case 'n':
            mode = ALLOW_NONE;
            break;

          case 'p':
            mode = ALLOW_PRIVATE_NETWORKS;
            break;

          default:
            return EXIT_FAILURE;
        }
    }

    if (!*(argv + optind)) {
        eprintf("%s: no command specified to execute\n", argv0);
        return EXIT_FAILURE;
    }

    group = restriction_groups[mode];
    errno = 0;

    if (!(group_fields = getgrnam(group))) {
        if (errno == 0) {
            eprintf("%s: %s: group does not exist\n", argv0, group);
        } else {
            perror("getgrnam");
        }

        return EXIT_FAILURE;
    }

    if ((result = sysconf(_SC_NGROUPS_MAX)) == -1) {
        perror("sysconf: _SC_NGROUPS_MAX");
        return EXIT_FAILURE;
    } else {
        sc_ngroups_max = (size_t) result;
    }

    if (!(groups = malloc((sizeof(*groups) * sc_ngroups_max)))) {
        perror("malloc");
        return EXIT_FAILURE;
    }

    if ((result = getgroups((int) sc_ngroups_max, groups)) == -1) {
        perror("getgroups");
        return EXIT_FAILURE;
    } else {
        group_count = (size_t) result;
    }

    // On Linux, getgroups(2) reads "It is unspecified whether the effective
    // group ID of the calling process is included in the returned list." In a
    // setuid context, we do not want root's permissions spilling over via the
    // supplemental groups, so if the EGID differs from the GID, elide the EGID
    // from the list of groups. The kernel code for setgroups(2) does not have
    // a problem with duplicates, and modifying the entry is simpler than
    // deleting it.
    if (gid != egid) {
        for (size_t i = 0; i < group_count; i++) {
            if (groups[i] == egid) {
                groups[i] = gid;
            }
        }
    }

    // Add the restriction group to the supplemental groups if it is not
    // already included.
    for (size_t i = 0; i < group_count; i++) {
        if (groups[i] == group_fields->gr_gid) {
            goto set_groups;
        }
    }

    if (group_count >= sc_ngroups_max) {
        eprintf("%s: maximum number of groups reached\n", argv0);
        return EXIT_FAILURE;
    } else {
        groups[group_count++] = group_fields->gr_gid;
    }

set_groups:
    if (setgroups(group_count, groups) == -1) {
        perror("setgroups");
        return EXIT_FAILURE;
    }

    // Drop root privileges inherited from setuid and setgid contexts.
    if (gid != egid && setgid(gid) == -1) {
        perror("setgid");
        return EXIT_FAILURE;
    }

    if (uid != euid && setuid(uid) == -1) {
        perror("setuid");
        return EXIT_FAILURE;
    }

    // Try to guard against failed attempts to drop permissions.
    if (getuid() != geteuid()) {
        eprintf("%s: getuid() != geteuid(); aborting\n", argv0);
        return EXIT_FAILURE;
    }

    if (getgid() != getegid()) {
        eprintf("%s: getgid() != getegid(); aborting\n", argv0);
        return EXIT_FAILURE;
    }

    // We use "|" instead of "||" here because we want all probing failures to
    // be reported.
    if (udp_probing_enabled && (probe_ipv4() | probe_ipv6())) {
        return EXIT_FAILURE;
    }

    execvp(argv[optind], argv + optind);
    eprintf("%s: %s: %s\n", argv0, argv[optind], strerror(errno));
    return errno == ENOENT ? EXIT_COMMAND_NOT_FOUND : EXIT_EXEC_FAILURE;
}
