#!/bin/bash

set -e

if [ $# -eq 0 ] ; then
    BASEDIR="$(dirname "$(readlink -f "$0")")/.."
elif [ $# -eq 1 ] ; then
    BASEDIR="$1"
else
    echo "invalid arguments"
    exit 1
fi

cd "$BASEDIR"

if ! [ -f ./.gitlab-ci.yml ] ; then
    # we have no gitlab-ci. Probably this is not a git-checkout
    # but a dist'ed source-tree. Nothing to check.
    exit 0
fi

if ! ci-fairy --help 2>&1 >/dev/null ; then
    # ci-fairy not available. Cannot check.
    exit 0
fi


if [ "$NM_TEST_REGENERATE" == 1 ] ; then
    ci-fairy generate-template
    exit 0
fi

diff "./.gitlab-ci.yml" <(ci-fairy generate-template -o /dev/stdout)
