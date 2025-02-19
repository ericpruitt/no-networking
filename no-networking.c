/**
 * This tool adds supplemental groups to restrict network access of the process
 * it spawns.
 *
 * - Make: c99 -D_DEFAULT_SOURCE -lc no-networking.c -o no-networking
 */
#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    RESTRICTION_NO_NETWORKING,
    /**
     * Allow connections to loopback addresses.
     */
    RESTRICTION_LOOPBKACK_NETWORKING,
    /**
     * Allow connections to private addresses.
     */
    RESTRICTION_PRIVATE_NETWORKING,
} restriction_et;

static void usage(const char *program)
{
    printf("Usage: %s [-lnp] COMMAND [ARGUMENT]...\n", program);
    printf("       %s -h\n", program);
    printf("       %s -V\n", program);
    printf("       %s --help\n", program);
    printf("\n");
    printf("Options:\n");
    printf("  -h, -V, --help\n");
    printf("        Show this documentation and exit.\n");
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

    restriction_et restriction = RESTRICTION_NO_NETWORKING;

    const char *program = argc ? basename(argv[0]) : "no-networking";
    const char *restriction_groups[] = {
        [RESTRICTION_NO_NETWORKING] = NO_NETWORKING_GROUP,
        [RESTRICTION_LOOPBKACK_NETWORKING] = LOOPBACK_NETWORKING_GROUP,
        [RESTRICTION_PRIVATE_NETWORKING] = PRIVATE_NETWORKING_GROUP,
    };

    if (argc > 1) {
        for (char **arg = argv; *arg; arg++) {
            if (strcmp(*arg, "--") == 0) {
                break;
            } else if (strcmp(*arg, "--help") == 0) {
                usage(program);
                return EXIT_SUCCESS;
            }
        }
    }

    while ((option = getopt(argc, argv, "+hlnpV")) != -1) {
        switch (option) {
          case 'h':
          case 'V':
            usage(program);
            return EXIT_SUCCESS;

          case 'l':
            restriction = RESTRICTION_LOOPBKACK_NETWORKING;
            break;

          case 'n':
            restriction = RESTRICTION_NO_NETWORKING;
            break;

          case 'p':
            restriction = RESTRICTION_PRIVATE_NETWORKING;
            break;

          default:
            return EXIT_FAILURE;
        }
    }

    if (!*(argv + optind)) {
        fprintf(stderr, "%s: no command specified to execute\n", program);
        return EXIT_FAILURE;
    }

    group = restriction_groups[restriction];
    errno = 0;

    if (!(group_fields = getgrnam(group))) {
        if (errno == 0) {
            fprintf(stderr, "%s: %s: group does not exist\n", program, group);
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
        fprintf(stderr, "%s: maximum number of groups reached\n", program);
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
    assert(getuid() == geteuid());
    assert(getgid() == getegid());

    execvp(argv[optind], argv + optind);
    fprintf(stderr, "%s: %s: %s\n", program, argv[optind], strerror(errno));
    return errno == ENOENT ? EXIT_COMMAND_NOT_FOUND : EXIT_EXEC_FAILURE;
}
