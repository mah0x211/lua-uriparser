#!/bin/sh

set -e
set -x

cd deps/uriparser/
autoreconf -ivf
./configure --disable-doc --disable-test
make
make check
