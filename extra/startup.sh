#/bin/bash

#TODO stoud/stderr redirect does not work!

./src/tests/vos/testharness/clean_shm.sh

script -qc ./generated.x86/src/servers/registrar/registrar 2&> registrar.out &

sleep 0.1

script -qc ./generated.x86/src/servers/app/app_server 2&> app_server.out &

sleep 1

#script -qc ./generated.x86/src/servers/input/input_server 2&> input_server.out &

sleep 1

#script -qc ./generated.x86/src/apps/deskbar/Deskbar 2&> deskbar.out &
#sleep 1
#script -qc ./generated.x86/src/apps/pairs/Pairs 2&> pairs.out &
