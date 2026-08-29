#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_output=$(mktemp -d /tmp/cherryusb-tests.XXXXXX)
host_compiler=${CHERRYUSB_TEST_CC:-cc}

cleanup()
{
    rm -rf -- "$test_output"
}
trap cleanup EXIT HUP INT TERM

common_flags="-std=c99 -Wall -Wextra -Werror"
common_includes="-I$project_root/CherryUSB -I$project_root/CherryUSB/common -I$project_root/CherryUSB/core"

"$host_compiler" $common_flags $common_includes \
    -I"$project_root/CherryUSB/class/hid" \
    "$project_root/tests/usbd_core_configuration_test.c" \
    -o "$test_output/usbd_core_configuration_test"
"$test_output/usbd_core_configuration_test"

"$host_compiler" $common_flags $common_includes \
    -I"$project_root/CherryUSB/class/hid" \
    "$project_root/tests/usbd_hid_default_test.c" \
    -o "$test_output/usbd_hid_default_test"
"$test_output/usbd_hid_default_test"

"$host_compiler" $common_flags $common_includes \
    -I"$project_root/CherryUSB/class/hid" \
    "$project_root/tests/usbd_hid_report_test.c" \
    "$project_root/CherryUSB/class/hid/usbd_hid.c" \
    -o "$test_output/usbd_hid_report_test"
"$test_output/usbd_hid_report_test"

"$host_compiler" $common_flags $common_includes \
    -I"$project_root/CherryUSB/class/msc" \
    "$project_root/tests/usbd_msc_transport_test.c" \
    -o "$test_output/usbd_msc_transport_test"
"$test_output/usbd_msc_transport_test"

"$host_compiler" $common_flags $common_includes \
    -I"$project_root/CherryUSB/port/ch32" \
    "$project_root/tests/usb_dc_usbhs_ep0_state_test.c" \
    -o "$test_output/usb_dc_usbhs_ep0_state_test"
"$test_output/usb_dc_usbhs_ep0_state_test"

echo "CherryUSB portable tests passed"
