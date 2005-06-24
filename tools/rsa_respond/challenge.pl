#!/usr/local/bin/perl -w
# $Id$
#
use strict;
use vars qw($VERSION %IRSSI);
use Irssi qw( settings_add_str settings_get_str signal_add signal_stop command_bind print );

$VERSION = '1.0';
%IRSSI = (
    authors => 'J. Johnston',
    contact => 'jeremy@cvs.freeworld.nu',
    name    => '/CHALLENGE Helper',
    description	=> 'Automate the response for /CHALLENGE',
    licence => 'BSD (Revised)',
);

settings_add_str ('rsa_respond', 'rsa_key', '');
settings_add_str ('rsa_respond', 'respond_prog', 'respond');

signal_add("server incoming", "handle_incoming");

command_bind challenge => \&challenge;

if (settings_get_str('rsa_key') eq "") {
    print ("Warning: rsa_key not set. /SET rsa_key /path/to/rsa.key");
}

sub handle_incoming {
    my ($server, $data) = @_;
    my ($sender, $msg, $target) = split(/ /, $data);

    if($data =~ m/^(.*) (.*) (.*) (.*)$/i)
    {
	if($2 eq "386")
	{
	    my $challenge = $4;
	    chomp($challenge);
	    $challenge =~ s/://;
	    my $keypath = settings_get_str('rsa_key');
	    my $respond = settings_get_str('respond_prog');
	    $server->command("QUOTE CHALLENGE +".`respond $keypath $challenge`);
	    signal_stop();
	}
    }
}

sub challenge {
    my ($data, $server, $witem) = @_;

    $server->command ("QUOTE CHALLENGE $data");
}
