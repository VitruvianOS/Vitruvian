#!/bin/sh

currUser=`whoami`

echo Deleting all unattached shm and sem segments for user $currUser

for segment in `ipcs -m  | grep '^[0-9]' | grep $currUser | cut -f2 -d' '`; do
	echo Deleting shared memory with shmid $segment
	ipcrm -m $segment
done

for segment in `ipcs -s  | grep '^[0-9]' | grep $currUser | cut -f2 -d' '`; do
	echo Deleting sem with semid $segment
	ipcrm -s $segment
done
