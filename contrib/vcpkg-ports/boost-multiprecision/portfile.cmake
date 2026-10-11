# Based on vcpkg baseline 120deac3062162151622ca4860575a33844ba10b.
# Backport https://github.com/boostorg/multiprecision/pull/667.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO boostorg/multiprecision
    REF boost-${VERSION}
    SHA512 a921d478338d0e039b6f43eaa5460c0778393f71ea64e22995a3f8e57fee8c70d277537f2d064bd3c235b3a2701b988a38e759afeaf4b11e36368bf5f077c134
    HEAD_REF master
    PATCHES
        optional-random.diff
        conforming-literals.patch
)

set(FEATURE_OPTIONS "")
boost_configure_and_install(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS}
)
