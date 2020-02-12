#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys


# This script is used to determine the current version of Memgraph. The script
# determines the current version using `git` automatically. The user can also
# manually specify a version override and then the supplied version will be
# used instead of the version determined by `git`. All versions (automatically
# detected and manually specified) can have a custom version suffix added to
# them.
#
# The current version can be one of either:
#   - release version
#   - development version
#
# The release version is either associated with a `release/X.Y` branch
# (automatic detection) or is manually specified. When the version is
# automatically detected from the branch a `.0` is appended to the version.
# Example 1:
#   - branch: release/0.50
#   - version: 0.50.0
# Example 2:
#   - manually supplied version: 0.50.1
#   - version: 0.50.1
#
# The development version is always determined using `git` in the following
# way:
#   - release version - nearest (older) `release/X.Y` branch version
#   - distance from the release branch - Z commits
#   - the current commit short hash
# Example:
#   - release version: 0.50.0 (nearest older branch `release/0.50`)
#   - distance from the release branch: 42 (commits)
#   - current commit short hash: 7e1eef94
#
# The script then uses the collected information to generate the versions that
# will be used in the binary, DEB package and RPM package.  All of the versions
# have different naming conventions that have to be met and they differ among
# each other.
#
# The binary version is determined using the following two templates:
#   Release version:
#     <VERSION>-<OFFERING>[-<SUFFIX>]
#   Development version:
#     <VERSION>+<DISTANCE>~<SHORTHASH>-<OFFERING>[-<SUFFIX>]
# Examples:
#   Release version:
#     0.50.1-community
#     0.50.1-enterprise
#     0.50.1-enterprise-veryimportantcustomer
#   Development version (master, 12 commits after release/0.50):
#     0.50.0+12~7e1eef94-community
#     0.50.0+12~7e1eef94-enterprise
#     0.50.0+12~7e1eef94-enterprise-veryimportantcustomer
#
# The DEB package version is determined using the following two templates:
#   Release version:
#     <VERSION>-<OFFERING>[-<SUFFIX>]-1
#   Development version (master, 12 commits after release/0.50):
#     <VERSION>+<DISTANCE>~<SHORTHASH>-<OFFERING>[-<SUFFIX>]-1
# Examples:
#   Release version:
#     0.50.1-community-1
#     0.50.1-enterprise-1
#     0.50.1-enterprise-veryimportantcustomer-1
#   Development version (master, 12 commits after release/0.50):
#     0.50.0+12~7e1eef94-community-1
#     0.50.0+12~7e1eef94-enterprise-1
#     0.50.0+12~7e1eef94-enterprise-veryimportantcustomer-1
# For more documentation about the DEB package naming conventions see:
#   https://www.debian.org/doc/debian-policy/ch-controlfields.html#version
#
# The RPM package version is determined using the following two templates:
#   Release version:
#     <VERSION>-1.<OFFERING>[.<SUFFIX>]
#   Development version:
#     <VERSION>-0.<DISTANCE>.<SHORTHASH>.<OFFERING>[.<SUFFIX>]
# Examples:
#   Release version:
#     0.50.1-1.community
#     0.50.1-1.enterprise
#     0.50.1-1.enterprise.veryimportantcustomer
#   Development version:
#     0.50.0-0.12.7e1eef94.community
#     0.50.0-0.12.7e1eef94.enterprise
#     0.50.0-0.12.7e1eef94.enterprise.veryimportantcustomer
# For more documentation about the RPM package naming conventions see:
#   https://docs.fedoraproject.org/en-US/packaging-guidelines/Versioning/
#   https://fedoraproject.org/wiki/Package_Versioning_Examples


def get_output(*cmd, multiple=False):
    ret = subprocess.run(cmd, stdout=subprocess.PIPE, check=True)
    if multiple:
        return list(map(lambda x: x.strip(),
                        ret.stdout.decode("utf-8").strip().split("\n")))
    return ret.stdout.decode("utf-8").strip()


def format_version(variant, version, offering, distance=None, shorthash=None,
                   suffix=None):
    if not distance:
        # This is a release version.
        if variant == "deb":
            # <VERSION>-<OFFERING>[-<SUFFIX>]-1
            ret = "{}-{}".format(version, offering)
            if suffix:
                ret += "-" + suffix
            ret += "-1"
            return ret
        elif variant == "rpm":
            # <VERSION>-1.<OFFERING>[.<SUFFIX>]
            ret = "{}-1.{}".format(version, offering)
            if suffix:
                ret += "." + suffix
            return ret
        else:
            # <VERSION>-<OFFERING>[-<SUFFIX>]
            ret = "{}-{}".format(version, offering)
            if suffix:
                ret += "-" + suffix
            return ret
    else:
        # This is a development version.
        if variant == "deb":
            # <VERSION>+<DISTANCE>~<SHORTHASH>-<OFFERING>[-<SUFFIX>]-1
            ret = "{}+{}~{}-{}".format(version, distance, shorthash, offering)
            if suffix:
                ret += "-" + suffix
            ret += "-1"
            return ret
        elif variant == "rpm":
            # <VERSION>-0.<DISTANCE>.<SHORTHASH>.<OFFERING>[.<SUFFIX>]
            ret = "{}-0.{}.{}.{}".format(
                version, distance, shorthash, offering)
            if suffix:
                ret += "." + suffix
            return ret
        else:
            # <VERSION>+<DISTANCE>~<SHORTHASH>-<OFFERING>[-<SUFFIX>]
            ret = "{}+{}~{}-{}".format(version, distance, shorthash, offering)
            if suffix:
                ret += "-" + suffix
            return ret


# Parse arguments.
parser = argparse.ArgumentParser(
    description="Get the current version of Memgraph.")
parser.add_argument(
    "--enterprise", action="store_true",
    help="set the current offering to enterprise (default 'community')")
parser.add_argument(
    "version", help="manual version override, if supplied the version isn't "
    "determined using git")
parser.add_argument(
    "suffix", help="custom suffix for the current version being built")
parser.add_argument(
    "--variant", choices=("binary", "deb", "rpm"), default="binary",
    help="which variant of the version string should be generated")
args = parser.parse_args()

offering = "enterprise" if args.enterprise else "community"

# Check whether the version was manually supplied.
if args.version:
    if not re.match(r"^[0-9]+\.[0-9]+\.[0-9]+$", args.version):
        raise Exception("Invalid version supplied '{}'!".format(args.version))
    print(format_version(args.variant, args.version, offering,
                         suffix=args.suffix), end="")
    sys.exit(0)

# Get current commit hashes.
current_hash = get_output("git", "rev-parse", "HEAD")
current_hash_short = get_output("git", "rev-parse", "--short", "HEAD")

# We want to find branches that exist on some remote and that are named
# `release/[0-9]+\.[0-9]+`.
branch_regex = re.compile(r"^remotes/[a-zA-Z0-9]+/release/([0-9]+\.[0-9]+)$")

# Find all existing versions.
versions = []
branches = get_output("git", "branch", "--all", multiple=True)
for branch in branches:
    match = branch_regex.match(branch)
    if match is not None:
        version = tuple(map(int, match.group(1).split(".")))
        master_branch_merge = get_output("git", "merge-base", "master", branch)
        versions.append((version, branch, master_branch_merge))
versions.sort(reverse=True)

# Check which existing version branch is closest to the current commit. We are
# only interested in branches that were branched out before this commit was
# created.
current_version = None
for version in versions:
    version_tuple, branch, master_branch_merge = version
    current_branch_merge = get_output(
        "git", "merge-base", current_hash, branch)
    master_current_merge = get_output(
        "git", "merge-base", current_hash, "master")
    # The first check checks whether this commit is a child of `master` and
    # the version branch was created before us.
    # The second check checks whether this commit is a child of the version
    # branch.
    if master_branch_merge == current_branch_merge or \
            master_branch_merge == master_current_merge:
        current_version = version
        break

# Determine current version.
if current_version is None:
    raise Exception("You are attempting to determine the version for a very "
                    "old version of Memgraph!")
version, branch, master_branch_merge = current_version
distance = int(get_output("git", "rev-list", "--count", "--first-parent",
                          master_branch_merge + ".." + current_hash))
version_str = ".".join(map(str, version)) + ".0"
if distance == 0:
    print(format_version(version_str, suffix=args.suffix), end="")
else:
    print(format_version(args.variant, version_str, offering,
                         distance=distance, shorthash=current_hash_short,
                         suffix=args.suffix),
          end="")