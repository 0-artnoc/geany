#!/usr/bin/env perl
# Copyright:	2008-2010, Nick Treleaven
# License:		GNU GPL V2 or later
# Warranty:		NONE

# Searches a ChangeLog file for a line matching 'matchstring', then matches
# all lines until two consecutive empty lines are found. The process then
# repeats until all matching blocks of text are found.
# Results are printed in reverse, hence in chronological order (as ChangeLogs
# are usually written in reverse date order).
#
# The resulting lines are then formatted to be easier to read and edit into a
# NEWS file.

# Example ChangeLog format:
#2009-04-03  Joe Author  <joe@example.net>
#
# * src/file.c, src/file.h,
#   src/another.c:
#   Some change description,
#   spanning several lines.
# * foo.c: Combined line.

use strict;
use warnings;

my $scriptname = "changelist.pl";
my $argc = $#ARGV + 1;

($argc == 2) or die <<END;
Usage:
$scriptname matchstring changelogfile >outfile

  matchstring is not case sensitive.
END

my ($matchstr, $infile) = @ARGV;

open(INPUT, $infile)
	or die "Couldn't open $infile for reading: $!\n";

my $entry;	# the current matching block of text
my @entries;	# changelog entries, one per date

# first parse each ChangeLog entry into an array

my $found = 0;	# if we're in a matching block of text
my $blank = 0;	# whether the last line was empty

while (<INPUT>) {
	my $line = $_;	# read a line, including \n char

	if (! $found) {
		($line =~ m/$matchstr/) and $found = 1;
	} else {
		if (length($line) <= 1) {	# current line is empty
			if ($blank > 0) {	# previous line was also empty
				push(@entries, $entry);	# append entry
				$entry = "";
				$found = 0;	# now look for next match
				$blank = 0;
			}
			else {
				$blank = 1;
			}
		}
	}
	if ($found) {
		$entry .= $line;
	}
}
close(INPUT);

# reformat entries
foreach $entry (reverse @entries) {
	my @lines = split(/\n/, $entry);
	my $fl = 0;	# in file list lines
	my $cm = 0; # in commit message lines

	foreach my $line (@lines){
		my $flset = $fl;

		# strip trailing space
		$line =~ s/\s+$//g;

		if (!$cm){
			# check if in filelist
			($line =~ m/ \* /) and $fl = 1;
			# join filelist together on one line
			$fl and ($line =~ s/^   / /);
			if ($fl and ($line =~ m/:/)){
				$fl = 0;
				# separate ' * foo.c: Some edit.' messages:
				if (!($line =~ m/:$/)) {
					($line =~ s/:/:\n*/);
				}
			}
			$fl and ($line =~ m/,$/) or $fl = 0;
		}
		if (!$flset){
			# Asterisk commit messages
			if (!$cm and ($line =~ m/^   /)){
				$cm = 1;
				$line =~ s/^(   )/$1* /;
			} else {
				$cm and ($line =~ s/^(   )/$1  /);	# indent continuing lines
			}
			$cm and ($line =~ m/\.$/) and $cm = 0;
		}
		#~ print $fl.','.$cm.','.$line."\n"; next;	# debug

		# change file list start char to easily distinguish between file list and commit messages
		$line =~ s/^ \* /@ /g;
		# strip <email> from date line
		$line =~ s/^([0-9-]+.*?)\s+<.+>$/$1/g;
		# remove indent
		$line =~ s/^   //g;

		if ($line ne ""){
			print $line;
			(!$fl) and print "\n";
		}
	}
	print "\n";
}
