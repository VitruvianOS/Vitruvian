#/bin/sh

cd /system/apps/unittester/


export LD_PRELOAD=./libcppunit.so

./UnitTester -l./libs BLocker > locker.log
./UnitTester -l./libs BNode > node.log
./UnitTester -l./libs BEntry > entry.log
./UnitTester -l./libs BDirectory > file.log
./UnitTester -l./libs BFile > file.log
./UnitTester -l./libs BPath > path.log
