#!/bin/sh

set -eu

mkdir -p /tmp/lv2test

./waf configure --lv2-user --prefix=/tmp/lv2test
./waf build
./waf install
