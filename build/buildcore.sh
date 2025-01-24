#!/bin/sh

set -e

basedir=`realpath ./generated.x86`

bold=$(tput bold)
normal=$(tput sgr0)

echo ${bold}Building core packages...

cd $basedir

ninja

cpack

echo ${bold}Core packages have been built!
