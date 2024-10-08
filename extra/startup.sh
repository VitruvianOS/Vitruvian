#/bin/bash

export LD_PRELOAD=./generated.x86/external/icu-cmake/icu_host/lib/libicudata.so.57

mkdir /dev/shm/vproc
mkdir /dev/shm/vproc/area

./src/tests/vos/testharness/clean_shm.sh
rm /dev/shm/vproc/area/*

script -qc ./generated.x86/src/servers/registrar/registrar 2&> registrar.out &

sleep 1

script -qc ./generated.x86/src/servers/app/app_server 2&> app_server.out &

#sleep 1

#script -qc ./generated.x86/src/servers/input/input_server 2&> input_server.out &

sleep 1

script -qc ./generated.x86/src/apps/deskbar/Deskbar 2&> deskbar.out &

sleep 1

script -qc ./generated.x86/src/apps/terminal/Terminal 2&> terminal.out &
script -qc ./generated.x86/src/apps/pairs/Pairs 2&> pairs.out &
script -qc ./generated.x86/src/apps/stylededit/StyledEdit 2&> stylededit.out &
