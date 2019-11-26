#!/bin/bash

set -e

dkms_name="hid-x1tab3-dkms"
dkms_version="5.3.7+x1tab3"

if dkms status -m $dkms_name -v $dkms_version | egrep '(added|built|installed)' >/dev/null ; then
  # if dkms bindings exist, remove them
  dkms remove $dkms_name/$dkms_version --all
fi
