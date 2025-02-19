#!/bin/sh
# This script implements logic that is responsible for configuring iptables
# during installation and removal of the package, and it also acts as the
# source of truth for the names of the groups used in the no-networking
# utility; running the script with no arguments will write "#define" directives
# with the group names to standard output.
set -e -f -u

# Names of the groups used to apply restrictions.
LOOPBACK_NETWORKING_GROUP="loopback-networking"
NO_NETWORKING_GROUP="no-networking"
PRIVATE_NETWORKING_GROUP="private-networking"

# Name of the chain used to enforce restrictions.
CHAIN="no-networking"
# Chain to which the restrictions are appended.
PARENT_CHAIN="OUTPUT"
# Name of the intermediate chain used to stage new rules.
STAGING_CHAIN="$CHAIN-staging"

LOOPBACK_ADDRESSES="
    127.0.0.0/8
    ::1
"

# Essentially anything that would not necessarily connect to the internet.
PRIVATE_NETWORKS="
    $LOOPBACK_ADDRESSES
    10.0.0.0/8
    169.254.0.0/16
    172.16.0.0/12
    192.168.0.0/16
    198.18.0.0/15
    fc00::/7
    fe80::/10
"

# When given a list of IPv4 and/or IPv6 CIDR ranges, the output is amended to
# include the mapped IPv6 versions of any IPv4 CIDR ranges.
#
# Arguments:
# - $@: List of IPv4 and/or IPv6 CIDR ranges.
#
# Output: Amended list of ranges.
#
expand_ips()
{
    echo "$@"

    for cidr in "$@"; do
        if [ "${cidr##*:*}" ]; then
            address="${cidr%/*}"
            mask="${cidr#*/}"
            echo "::ffff:$address/$((mask + 96))"
        fi
    done
}

# Create the groups needed to apply network restrictions if they do not already
# exist.
#
create_groups()
{
    groups="
        $LOOPBACK_NETWORKING_GROUP
        $NO_NETWORKING_GROUP
        $PRIVATE_NETWORKING_GROUP
    "

    for group in $groups; do
        if ! getent group "$group" >/dev/null 2>&1; then
            addgroup --quiet --system "$group"
        fi
    done
}

# Command for executing an iptables command that chooses whether to execute
# iptables or ip6tables based on the arguments. This function should only be
# used if there is an IP address in the arguments.
#
# Arguments:
# - $@: Arguments for iptables or ip6tables.
#
auto()
{
    # Only IPv6 rules will include a colon in the arguments.
    case "$*" in
      *:*)  ip6tables "$@" ;;
      *)    iptables "$@" ;;
    esac
}

# Wrapper used to execute a command using both iptables and ip6tables.
#
# Arguments:
# - $@: Arguments for iptables and ip6tables.
#
both()
{
    iptables "$@"
    ip6tables "$@"
}

# Destroy a chain in both iptables and ip6tables. This function does not report
# errors even if the specified chain does not exist.
#
# Arguments:
# - $1: Name of the chain to destroy.
#
destroy_chain()
{
    chain="$1"

    {
        both -D "$PARENT_CHAIN" -j "$chain" || :
        both -F "$chain" || :
        both -X "$chain" || :
    } >/dev/null 2>&1
}

# Create or update the chain used to enforce network restrictions. If the chain
# already exists, this function updates its configuration in a manner than
# ensures there is no loss in continuity of the restrictions.
#
create_chain()
{
    # We create the rules in a staging chain first so, if there is an existing
    # "no-networking" chain, its restrictions remain in effect while the new
    # chain is being populated.
    destroy_chain "$STAGING_CHAIN"
    both -N "$STAGING_CHAIN"

    both -A "$STAGING_CHAIN" \
         -m owner \
         --gid-owner "$NO_NETWORKING_GROUP" \
         --suppl-groups \
         -j DROP

    for destination in $(expand_ips $LOOPBACK_ADDRESSES); do
        auto -A "$STAGING_CHAIN" \
             -m owner \
             --gid-owner "$LOOPBACK_NETWORKING_GROUP" \
             --suppl-groups \
             -d "$destination" \
             -j RETURN
    done

    both -A "$STAGING_CHAIN" \
         -m owner \
         --gid-owner "$LOOPBACK_NETWORKING_GROUP" \
         --suppl-groups \
         -j DROP

    for destination in $(expand_ips $PRIVATE_NETWORKS); do
        auto -A "$STAGING_CHAIN" \
             -m owner \
             --gid-owner "$PRIVATE_NETWORKING_GROUP" \
             --suppl-groups \
             -d "$destination" \
             -j RETURN
    done

    both -A "$STAGING_CHAIN" \
         -m owner \
         --gid-owner "$PRIVATE_NETWORKING_GROUP" \
         --suppl-groups \
         -j DROP

    # Now that all of the rules have been defined, delete the original chain if
    # it exists and rename the newly created chain.
    both -A "$PARENT_CHAIN" -j "$STAGING_CHAIN"
    destroy_chain "$CHAIN"
    both -E "$STAGING_CHAIN" "$CHAIN"
}

main()
{
    if [ "${DPKG_MAINTSCRIPT_NAME:-}" ]; then
        context="${1:-}"

        case "$DPKG_MAINTSCRIPT_NAME" in
          preinst)
            trap 'destroy_chain "$STAGING_CHAIN"' EXIT
            create_groups
            create_chain
            netfilter-persistent save
          ;;

          postrm)
            if [ "$context" != "upgrade" ]; then
                destroy_chain "$STAGING_CHAIN"
                destroy_chain "$CHAIN"
                netfilter-persistent save
            fi
          ;;

          *)
            echo "$DPKG_MAINTSCRIPT_NAME: not implemented" >&2
            return 1
          ;;
        esac
    elif [ "$#" -ne 0 ]; then
        echo "${0##*/}: script accepts no argument when not run by dpkg" >&2
        return 1
    else
        echo "#define LOOPBACK_NETWORKING_GROUP \"$LOOPBACK_NETWORKING_GROUP\""
        echo "#define NO_NETWORKING_GROUP \"$NO_NETWORKING_GROUP\""
        echo "#define PRIVATE_NETWORKING_GROUP \"$PRIVATE_NETWORKING_GROUP\""
    fi
}

main "$@"
