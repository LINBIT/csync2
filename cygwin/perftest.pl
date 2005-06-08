#!/usr/bin/perl
#
# Simple performance testing utility for csync2 on cygwin..

$| = 1;

my $basedir = "/cygdrive/f/sharedata";
my $filenumber = 100000;

if ($ARGV[0] eq "create") {
	for my $i (0..$filenumber) {
		my $f = crypt($i, "00");
		$f =~ s:[^a-zA-Z0-9]:_:g;
		$f =~ s:^..:$basedir/:;

		open(F, ">$f") or die $!;
		print F "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 $f\n" x 100;
		close F;

		if ( $i < $filenumber ) {
			printf("%s%5d ", $i ? "\n" : "", $i) if $i % 10000 == 0;
			print "." if $i % 200 == 0;
		} else {
			print "\n";
		}
	}
}

