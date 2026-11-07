#!/usr/bin/perl
# Copyright 2026, Dario Casalinuovo. All rights reserved.
#
# dh sequence add-on for V\OS packages: dh $@ --with vos

use strict;
use warnings;
use Debian::Debhelper::Dh_Lib;

insert_before('dh_dwz', 'dh_vosattrs');
insert_before('dh_dwz', 'dh_vosstrip');
remove_command('dh_dwz');
remove_command('dh_strip');
insert_before('dh_shlibdeps', 'dh_vosshlibdeps');
remove_command('dh_shlibdeps');

1;
