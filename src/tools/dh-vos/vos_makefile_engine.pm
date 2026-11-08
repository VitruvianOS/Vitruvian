# Copyright 2026, Dario Casalinuovo. All rights reserved.
#
# debhelper buildsystem for legacy Makefile-Engine apps:
#   dh $@ --with vos --buildsystem=vos_makefile_engine
#
# The app's makefile is never patched; the engine is steered with
# command-line overrides. Installs:
#   the binary          -> /system/apps/<NAME>
#                          (/system/add-ons/<subdir>/<NAME> when debian/rules
#                          exports VOS_ADDON_SUBDIR, e.g. Tracker)
#   obj/<sig>/*.catalog -> /system/data/locale/catalogs/<sig>/<lang>.catalog
# Attributes come from the embedded resources (dh_vosattrs).

package Debian::Debhelper::Buildsystem::vos_makefile_engine;

use strict;
use warnings;

use parent qw(Debian::Debhelper::Buildsystem);
use File::Basename qw(basename dirname);
use File::Spec;

my $tools = '/usr/lib/vos/buildtools';

sub DESCRIPTION {
	"legacy Makefile-Engine (V\\OS)";
}

sub new {
	my $class = shift;
	my $this = $class->SUPER::new(@_);
	$this->enforce_in_source_building();
	return $this;
}

# mimeset needs nexus; the vos trigger runs it on install.
sub _overrides {
	my @o = ("DEVEL_DIRECTORY=/", "MIMESET=true", "OBJ_DIR=obj");
	# executable shared object so the Deskbar can load the replicant
	push(@o, "TYPE=SHARED") if $ENV{VOS_RUNNABLE_ADDON};
	return @o;
}

# The .interp object runnable-addon.mk links in; ld emits PT_INTERP for a
# -shared link only from an input object.
sub _interp_object {
	my $this = shift;
	return unless $ENV{VOS_RUNNABLE_ADDON};
	my $arch = `dpkg-architecture -qDEB_HOST_ARCH`;
	chomp($arch);
	my %interp = (
		amd64 => "/lib64/ld-linux-x86-64.so.2",
		arm64 => "/lib/ld-linux-aarch64.so.1",
		riscv64 => "/lib/ld-linux-riscv64-lp64d.so.1");
	my $path = $interp{$arch}
		or die "Makefile-Engine: no interpreter for $arch\n";
	$this->doit_in_sourcedir("sh", "-c",
		'printf "%s\n" ".section .interp,\"a\"" ".asciz \"$1\"" '
		. '".section .note.GNU-stack,\"\",%progbits" > runnable_addon_interp.S '
		. '&& cc -c runnable_addon_interp.S -o runnable_addon_interp.o',
		"sh", $path);
}

# Legacy apps rely on libbe pulling in libroot and libm; Debian's ld does
# not resolve them transitively. The engine appends LINK_LIBS.
sub _env {
	return (PATH => "$tools:$ENV{PATH}", LINK_LIBS => "-lroot -lm");
}

sub _makefile_path {
	my $this = shift;
	for my $name (qw(Makefile makefile GNUmakefile)) {
		my $path = $this->get_sourcepath($name);
		return $path if -f $path;
	}
	return $this->get_sourcepath("Makefile");
}

sub check_buildsystem {
	my $this = shift;
	my $mk = $this->_makefile_path;
	open(my $fh, "<", $mk) or return 0;
	while (my $line = <$fh>) {
		return 1 if $line =~ /makefile-engine/;
	}
	return 0;
}

# NAME, APP_MIME_SIG and LOCALES as make computes them.
sub _mkvars {
	my $this = shift;
	my %vars;
	my $mk = $this->_makefile_path;
	open(my $fh, "-|", "make", "-C", dirname($mk), "-pn",
		"-f", basename($mk), $this->_overrides) or die "make -pn: $!";
	while (my $line = <$fh>) {
		next unless $line =~ /^([A-Za-z_][A-Za-z0-9_]*)\s*[:?+]?=\s*(.*)$/;
		my ($key, $value) = ($1, $2);
		$value =~ s/^"(.*)"$/$1/;
		$value =~ s/^'(.*)'$/$1/;
		$value =~ s/\\ / /g;
		$vars{$key} = $value;
	}
	close($fh);
	return \%vars;
}

sub build {
	my $this = shift;
	my %env = $this->_env;
	local @ENV{keys %env} = values %env;
	$this->_interp_object;
	$this->doit_in_builddir("make", $this->_overrides, @_);
	# Catalogs straight from the shipped catkeys; the engine's own target
	# would regenerate them with collectcatkeys.
	my $vars = $this->_mkvars();
	my $sig = $vars->{APP_MIME_SIG};
	return 1 unless $sig && ($vars->{LOCALES} // '') =~ /\S/;
	my %wanted = map { $_ => 1 } split(/\s+/, $vars->{LOCALES});
	for my $ck (glob($this->get_sourcepath("locales/*.catkeys"))) {
		my ($lang) = basename($ck) =~ /(.+)\.catkeys$/;
		next unless $wanted{$lang};
		$this->doit_in_builddir("mkdir", "-p", "obj/$sig");
		$this->doit_in_builddir("linkcatkeys", "-o",
			"obj/$sig/$lang.catalog", "-s", $sig,
			"-l", $lang, "locales/$lang.catkeys");
	}
	return 1;
}

sub _has_clean {
	my $this = shift;
	my $dir = $this->get_buildpath;
	return system("sh", "-c",
		'd=$1; shift; make -C "$d" -n "$@" clean >/dev/null 2>&1',
		"sh", $dir, $this->_overrides) == 0;
}

sub clean {
	my $this = shift;
	my %env = $this->_env;
	local @ENV{keys %env} = values %env;
	$this->doit_in_sourcedir("rm", "-f", "runnable_addon_interp.S",
		"runnable_addon_interp.o") if $ENV{VOS_RUNNABLE_ADDON};
	# not every engine makefile has a clean target
	return 1 unless $this->_has_clean;
	$this->doit_in_builddir("make", $this->_overrides, "clean");
	return 1;
}

sub install {
	my ($this, $destdir) = @_;
	my %env = $this->_env;
	local @ENV{keys %env} = values %env;
	my $vars = $this->_mkvars();
	my $name = $vars->{NAME}
		or die "Makefile-Engine: NAME is not set\n";
	my $bin = basename($name);

	my $root = "system/apps";
	if (my $subdir = $ENV{VOS_ADDON_SUBDIR}) {
		die "Makefile-Engine: VOS_ADDON_SUBDIR is not a bare name\n"
			unless $subdir =~ m{^[A-Za-z0-9_][A-Za-z0-9_.-]*$};
		$root = "system/add-ons/$subdir";
	}
	$this->doit_in_builddir("install", "-m", "0755", "-D",
		"obj/$name", "$destdir/$root/$bin");

	my $sig = $vars->{APP_MIME_SIG};
	return 1 unless $sig;
	for my $cat (glob($this->get_buildpath("obj/$sig/*.catalog"))) {
		my ($lang) = $cat =~ m{([^/]+)\.catalog$};
		$this->doit_in_builddir("install", "-m", "0644", "-D",
			File::Spec->rel2abs($cat),
			"$destdir/system/data/locale/catalogs/$sig/$lang.catalog");
	}
	return 1;
}

1;
