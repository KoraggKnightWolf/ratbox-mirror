#!/usr/bin/perl 

#---------------------------------------------------------------------------
# bantool.pl - Swiss army knife for ircd-ratbox-3 ban.db
#
# Copyright (C) 2007 Daniel J Reidy <dubkat@soulfresh.net>
# Copyright (C) 2007 ircd-ratbox development team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#---------------------------------------------------------------------------

#   DIRECTIONS: ./bantool -m for instructions.
#      CREATED:  2007-02-27 21:28:48 EST
#     REVISION:  $Id$

# Much thanks to efnet.port80.se for supplying configuration files used during development.

use strict;
use warnings;
use Getopt::Long;
use Pod::Usage;
use DBI;
use constant 
{
	BAN_DB  => 'ban.db',
	VERSION => '1.4',
	RED     => "\033[01;31m",
	GREEN   => "\033[01;32m",
	CYAN    => "\033[01;36m",
	WHITE   => "\033[01;37m",
	NORM    => "\033[0;0m",
};

my ($handle, @configs, $help, $man, $import,
	$pretend, $export, $version, $db, $noexist ) = undef;
my ($rules, $dupes) = (0,0);

@configs = qw( kline dline xline resv );

GetOptions(
	'help|h'        => \$help,
	'man|m'         => \$man,
	'import|i=s'    => \$import,
	'dest|d=s'      => \$db,
	'destination=s' => \$db,
	'export|e=s'    => \$export,
	'pretend|p'     => \$pretend,
	'version|v'     => \$version,
);

pod2usage(1) if (!defined($man) && $help);
pod2usage(-exitstatus => 0, -verbose => 2) if $man;
pod2usage(-exitstatus => 1, -verbose => 1)
	if (!defined($import) && !defined($version) && !defined($export));

print STDERR "\n";
show_version() if defined($version);
defined($pretend) ? info("Pretending...") : info("Running...");
$handle = init();

import_configs()    if defined($import);
export_ircscript()  if defined($export) && $export =~ m/^irc/i;
export_legacy()     if defined($export) && $export =~ m/^conf/i;

#===  FUNCTION  ================================================================
#         NAME:  init
#      PURPOSE:  (in)sanity checks
#  DESCRIPTION:  perform basic sanity checks of command line parameters.
#   PARAMETERS:  NULL
#      RETURNS:  DBD::SQLite handle ($handle)
#===============================================================================
sub init {
	throw("Conflicting tasks: --export and --import used at the same time") if (defined($import) and defined($export));

	throw("The ratbox config directory you are trying to import does not exist.",
	"Please verify that '$import' is the correct path.") if ( defined($import) && !-d $import );

	chop($import) if (defined($import) && $import =~ m!/$!);

	if ( !defined($db) )  {
		throw(
			"Expected ratbox-3 conf directory was not automaticlly found",
			"Please define path with '-d PATH'") if (! -d '../etc' );
		
		info("ratbox config directory automaticlly found.");
		$db = "../etc/" . BAN_DB;
	}
	elsif ( -d $db ) {
		$db =~ s!/$!!;
		$db = "$db/" . BAN_DB if ( -d $db );
	}
	else {
		throw(
			"The directory you specified with -d does not exist",
			"For example '-d /opt/ircd/etc'",
			"Please verify the path and try again");
	}
	
	throw(
		"You did not specify an old ratbox config dir to import,",
		"and the database does not exist in the location you specified.",
		"Use --import to import to the database or check your destination path.") 
		if ( ! -e $db && !defined($import) );

	$noexist = 1 unless ( -e $db );
	my $handle = defined($pretend) ? 
	             undef : 
	             DBI->connect("dbi:SQLite:dbname=$db", undef, undef, { AutoCommit => 1 });
	
	create_bandb($handle) if defined($noexist) && !defined($pretend);
	return $handle;
}

#===  FUNCTION  ================================================================
#         NAME:  import_configs
#      PURPOSE:  import legacy ircd-ratbox ban config files without needless 
#                duplication.
#   PARAMETERS:  void
#      RETURNS:  exits with 0 on success, 1 on failure.
#===============================================================================
sub import_configs {
	my $err = 0;
	my $query;
	for my $conf (@configs) {
		my $doConf = "$import/$conf.conf";
		
		if ( not -e $doConf ) {
			infowarn("     Failed to open $doConf");
			$err++;
			next;
		}
		
		open FH, "<", $doConf;
		while (<FH>) 
		{
			my ($mask1,$mask2,$oper,$time,$reason) = undef;
		
			# switch-ish
			if ($conf eq 'kline') {
				$_ =~ m/"(.+)","(.+)","(.*)",".*",".*","(.+)",(\d+)/;
				($mask1,$mask2,$reason,$oper,$time) = ($1,$2,$3,$4,$5);
			}
		
			if ($conf eq 'dline') {
				$_ =~ m/"(.+)","(.+)",".*",".*","(.+)",(\d+)/;
				($mask1,$reason,$oper,$time) = ($1,$2,$3,$4);
			}
			
			if ($conf eq 'xline') {
				$_ =~ m/"(.+)","\d*","(.+)","(.+)",(\d+)/;
				($mask1,$reason,$oper,$time) = ($1,$2,$3,$4);
			}
		
			if ($conf eq 'resv') {
				$_ =~ m/"(.+)","(.+)","(.+)",(\d+)/;
				($mask1,$reason,$oper,$time) = ($1,$2,$3,$4);
			}
			
			$mask2 ||= '';

			printf(
			       "%s Type:%s   %s%s%s\n".
			       "%s Mask:%s   %s%s %s%s\n".
			       "%s Reason:%s %s%s%s\n".
			       "%s Oper:%s   %s%s%s\n".
			       "%s Time:%s   %s%s%s\n\n",
		
			       CYAN,NORM, WHITE,$conf,NORM,
			       CYAN,NORM, WHITE,$mask1,$mask2,NORM,
			       CYAN,NORM, WHITE,$reason,NORM,
			       CYAN,NORM, WHITE,$oper,NORM,
			       CYAN,NORM, WHITE,$time,NORM
			) if $pretend;
		
			my $result = undef;
			if ( !defined($pretend) ) {
				# if i created the database, there wont be any pre-existing bans.
				unless ($noexist) {
					if ( defined($mask2) ) {
						$query = $handle->prepare("SELECT COUNT(*) AS count FROM $conf WHERE mask1=? AND mask2=?");
						$query->execute($mask1,$mask2);
					} else {
						$query = $handle->prepare("SELECT COUNT(*) AS count FROM $conf WHERE mask1=?");
						$query->execute($mask1);
					}
					$result = $query->fetch;
				}

				if ( !defined($result) || $result->[0] == 0 ) {
					$query = $handle->prepare(
					                  "INSERT INTO $conf (mask1,mask2,reason,oper,time) ".
					                  "VALUES (?,?,?,?,?)") || throw("\$handle->prepare failed!", $!);
					$query->execute($mask1, $mask2, $reason, $oper, $time) || throw("\$query->execute failed!", $!);
					$rules++;
				}

				$dupes++ if ( defined($result) && $result->[0] > 0 );
			}
		}
		close FH;
		info("     Imported $doConf") if (!defined($pretend));
	}
	throw("There were no ratbox ban config files located at '$import'") if ($err == 4);
	info(
		sprintf("New Bans Imported: %s%d%s", CYAN, $rules, NORM),
		sprintf("Duplicate Bans Skipped: %s%d%s", RED, $dupes, NORM),
		"Done.\n"
	);
	exit(0);
}

#===  FUNCTION  ================================================================
#         NAME:  info
#      PURPOSE:  pretty-print informative messages.
#   PARAMETERS:  array of strings.
#      RETURNS:  void
#===============================================================================
sub info {
	no warnings;
	printf STDERR ("%s*%s %s%s%s\n", GREEN,NORM,WHITE,$_,NORM) for @_;
}

#===  FUNCTION  ================================================================
#         NAME:  infowarn
#      PURPOSE:  pretty-print warning messages.
#   PARAMETERS:  array of strings.
#      RETURNS:  void
#===============================================================================
sub infowarn {
	no warnings;
	printf STDERR ("%s*%s %s%s%s\n", RED,NORM,WHITE, $_,NORM) for @_;
}

#===  FUNCTION  ================================================================
#         NAME:  throw
#      PURPOSE:  pretty-print fatal error messages, and exit.
#   PARAMETERS:  array of strings.
#      RETURNS:  exits with 1
#===============================================================================
sub throw {
	no warnings;
	printf STDERR ("%s*%s %s%s%s\n", RED,NORM, WHITE,$_,NORM) for @_;
	printf STDERR ("%s*%s%s Finished with Errors!%s\n\n", RED,NORM,WHITE,NORM);
	exit(1);
}

#===  FUNCTION  ================================================================
#         NAME:  create_bandb
#      PURPOSE:  create ircd-ratbox-3 ban.db
#   PARAMETERS:  DBI $handle
#      RETURNS:  void
#===============================================================================
sub create_bandb {
	my $handle = $_[0];
	info("Creating non-existant ratbox ban database: $db");
	if (!defined($pretend)) {
		$handle->do("CREATE TABLE kline (mask1 TEXT, mask2 TEXT, oper TEXT, time INTEGER, reason TEXT)");
		$handle->do("CREATE TABLE dline (mask1 TEXT, mask2 TEXT, oper TEXT, time INTEGER, reason TEXT)");
		$handle->do("CREATE TABLE xline (mask1 TEXT, mask2 TEXT, oper TEXT, time INTEGER, reason TEXT)");
		$handle->do("CREATE TABLE resv  (mask1 TEXT, mask2 TEXT, oper TEXT, time INTEGER, reason TEXT)");
	}
	return;
}

#===  FUNCTION  ================================================================
#         NAME:  export_ircscript
#      PURPOSE:  print commands suitable for dumping into an irc client
#   PARAMETERS:  void
#      RETURNS:  exit's with 0 on success.
#===============================================================================
sub export_ircscript {
	info("Exporting IRC Client Compatable Dumpfile.");
	for (@configs) { 
		my $query = $handle->prepare("SELECT mask1,mask2,reason FROM $_");
		$query->execute;
		while (my ($mask1, $mask2, $reason) = $query->fetchrow_array) {
			$mask1 =~ s! !\\s!g if ($_ eq 'xline');
			printf STDOUT ("/QUOTE KLINE %s@%s :%s \n", $mask1, $mask2, $reason) if ($_ eq 'kline');
			printf STDOUT ("/QUOTE DLINE %s :%s \n", $mask1, $reason) if ($_ eq 'dline');
			printf STDOUT ("/QUOTE XLINE %s :%s \n", $mask1, $reason) if ($_ eq 'xline');
			printf STDOUT ("/QUOTE RESV %s :%s \n", $mask1, $reason)  if ($_ eq 'resv');
		}
	}
	info("Done.");
	exit(0);
}	

#===  FUNCTION  ================================================================
#         NAME:  export_legacy
#      PURPOSE:  reproduce legacy style config files for redistribution
#   PARAMETERS:  void
#      RETURNS:  exit's with 0 on success.
#===============================================================================
sub export_legacy {
	info("Exporting to Legacy Config Files:");
	my ($mask1,$mask2,$oper,$time,$reason);
	for (@configs) {
		my $filename = sprintf("exported_%s.conf", $_);
		open FH, ">", $filename or throw("Failed to open $filename for export", $!);

		my $query = $handle->prepare("SELECT mask1,mask2,oper,time,reason FROM $_ ORDER BY time");
		$query->execute;
		while ( ($mask1,$mask2,$oper,$time,$reason) = $query->fetchrow_array ) {
			my $timestamp = gmtime($time);
			printf FH ("\"%s\",\"%s\",\"%s\",\"\",\"%s\",\"%s\",%d\n", $mask1, $mask2, $reason, $timestamp, $oper, $time) if ($_ eq 'kline');
			printf FH ("\"%s\",\"%s\",\"\",\"%s\",\"%s\",%d\n", $mask1, $reason, $timestamp, $oper, $time) if ($_ eq 'dline');
			printf FH ("\"%s\",\"0\",\"%s\",\"%s\",%d\n", $mask1, $reason, $oper, $time) if ($_ eq 'xline');
			printf FH ("\"%s\",\"%s\",\"%s\",%d\n",$mask1, $reason, $oper, $time) if ($_ eq 'resv');
		}
		close FH;
		info("    Wrote $filename");
	}
	info("Done.");
	exit(0);
}

#===  FUNCTION  ================================================================
#         NAME:  show_version
#      PURPOSE:  figure it out.
#   PARAMETERS:  void
#      RETURNS:  exits with 0
#===============================================================================
sub show_version {
	info(
		"bantool.pl version " . VERSION,
		"Copyright (C) 2007 Daniel J Reidy <dubkat\@soulfresh.net>",
		"Copyright (C) 2007 ircd-ratbox development team",
		undef,
		"This program is distributed in the hope that it will be useful,",
		"but WITHOUT ANY WARRANTY; without even the implied warranty of",
		"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the",
		"GNU General Public License for more details.",
	);
	print STDERR "\n";
	exit(0);
}

#---------------------------------------------------------------------------
#  Pod Documentation.
#---------------------------------------------------------------------------

__END__

=head1 NAME

bantool - Swiss army knife for ircd-ratbox-3 ban.db

=head1 DESCRIPTION

This perl script will parse your old ircd-ratbox versions 1 and 2 kline.conf,
dline.conf, xline.conf, resv.conf and convert it to the new ircd-ratbox-3
sqlite3 database. It also includes some additional tools to generate some
basic reports such as which opers place how many klines, and the ability
to export the database back to ratbox-2 format or an IRC Client dump for 
redistribution (ie, for new linking servers).

=head1 SYNOPSIS

./bantool.pl [options] 

=head1 REQUIRED MODULES

o B<Getopt::Long> - Likely included with your distribution's perl.

o B<DBD::SQLite>  - Install from cpan or with your distribution's package manager (portage, yum, ports, apt, etc).

o B<Pod::Usage>   - Likely included with your distribution's perl.

o B<DBI>          - Install from cpan or with your distribution's package manager (portage, yum, ports, apt, etc).

=head1 TIPS

Place this script in the same directory as your ircd binary [/path/to/ratbox/bin] and it
should detect your ban database automaticlly so you don't need to specify it's location on the commandline.

=head1 OPTIONS

=over 8

=item B<-h|--help>

Brief usage help.

=item B<-m|--man>

View fully fledged perldoc page.

=item B<-i|--import [path]>

Specify location of legacy ircd-ratbox /etc directory.

=item B<-d|--dest|--destination [path]>

Specify location of your new ircd-ratbox-3 etc directory that will contain ban.db.  It should 
be something like /home/user/ircd-ratbox-3/etc.  If you havn't run ircd-ratbox-3
yet, the database will not exist so it will be automaticlly created for you if you are doing
an --import. This option is only required if bantool does not find it automaticlly.

=item B<-p|--pretend>

Parse the old pre ratbox-3 config files and show what would be done.

=item B<-e|--export [irc | conf]>

if 'B<irc>' is selected, this script will print the commands suitable for dumping into an irc client
to duplicate the database. If 'B<conf>' is selected, then the script will write the following files:
B<exported_kline.conf>, B<exported_dline.conf>, B<exported_xline.conf>, B<exported_resv.conf> which could be
used to replace, or appended to, the appropriate ircd-ratbox-2 or ircd-hybrid-7 config files. These files
will created in the current directory.

=head1 EXAMPLES

=head2 Import from ratbox-2 to ratbox-3

./bantool.pl --import /opt/ratbox-2/etc

=head2 Same as above, but only show what would happen

./bantool.pl --import /opt/ratbox-2/etc --pretend

=head2 Create script to dump into an IRC Client

./bantool.pl --export irc > bans.txt

=head2 Create legacy kline.conf etc.

./bantool.pl --export conf

exported_*.conf files will be created in the current directory.

=head1 BUGS

If you find any bugs or have any feature requests, email the author with 'ratbox bantool' in the subject.

=head1 AUTHORS

Daniel J Reidy <dubkat@soulfresh.net> ('dubkat' on EFnet & IRCSource)

=head1 LICENSE

$Id$

Copyright (C) 2007 Daniel J Reidy <dubkat@soulfresh.net>
Copyright (C) 2007 ircd-ratbox development team

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software

=cut

