#!/usr/bin/env bash

# Copyright 2012 Red Hat Inc., Durham, North Carolina.
# All Rights Reserved.

. $srcdir/../../../test_common.sh

set -e -o pipefail

function test_api_xccdf_tailoring {
    local INPUT=$srcdir/$1
    local TAILORING=$srcdir/$2
    local PROFILE=$3
    local EXPECTED_PASS=$4

    local TMP_RESULTS=`mktemp`
    $OSCAP xccdf eval --tailoring $TAILORING --profile $PROFILE --results $TMP_RESULTS $INPUT
    if [ "$?" != "0" ]; then
        return 1
    fi

    cat $TMP_RESULTS

    local PASS_COUNT=$($XPATH $TMP_RESULTS 'count(//result[text()="pass"])')
    rm -f $TMP_RESULTS

    if [ "$PASS_COUNT" == "$EXPECTED_PASS" ]; then
        return 0
    fi

    return 1
}
# Testing.

test_init "test_api_xccdf_tailoring.log"

test_run "test_api_xccdf_tailoring_unselecting" test_api_xccdf_tailoring simple-xccdf.xml simple-tailoring.xml xccdf_org.open-scap_unselecting_profile 0
test_run "test_api_xccdf_tailoring_default" test_api_xccdf_tailoring simple-xccdf.xml simple-tailoring.xml xccdf_org.open-scap_default_profile 1

test_exit
