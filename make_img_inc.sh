#!/usr/bin/env sh

DIR=$(dirname $0)
cat "$1" | xxd -i > $DIR/img.inc
