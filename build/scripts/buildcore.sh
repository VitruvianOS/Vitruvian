#!/bin/sh
set -e

basedir=`realpath ./`

bold=$(tput bold)

echo ${bold}Building core packages...

cd $basedir

ninja

cpack

echo ${bold}Core packages have been built!
