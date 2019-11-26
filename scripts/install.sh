#!/bin/bash

set -e
set -x

dkms_name="hid-x1tab3-dkms"
dkms_version="5.3.7+x1tab3"
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# add
if ! dkms status -m $dkms_name -v $dkms_version | egrep '(added|built|installed)' >/dev/null ; then
  dkms add ${DIR}/../linux/drivers/hid
fi

# build
if ! dkms status -m $dkms_name -v $dkms_version  | egrep '(built|installed)' >/dev/null ; then
  dkms build $dkms_name/$dkms_version
fi

# install
if ! dkms status -m $dkms_name -v $dkms_version  | egrep '(installed)' >/dev/null; then
  dkms install --force $dkms_name/$dkms_version
fi
