#!/bin/sh
# Simple and straight forward openssl cert generator for ircd-ratbox
# Copyright (C) 2008-2015 ircd-ratbox development team
# $Id: genssl.sh 28847 2015-10-20 18:50:44Z androsyn $

if [ $# -eq 0 ]; then
	echo
	echo "usage: $0 <nickname> [<LENGTH_IN_DAYS_KEYS_ARE_VALID>]"
	echo "       default lenth of time keys are valid is 365 days."
	echo
	exit 1;
fi

SERVER="$1"

SSL_DAYS_VALID="${2:-365}"

echo
echo "Generating 4096-bit self-signed RSA key for ${SERVER}... "
openssl req -sha256 -new -newkey rsa:4096 -days ${SSL_DAYS_VALID} -nodes -x509 -keyout ${SERVER}.pem  -out ${SERVER}.pem
echo "Done creating self-signed cert"


echo
echo "Your SSL keys for ${SERVER} are valid for ${SSL_DAYS_VALID} days."
echo "If you wish to increase the number of days, run:"
echo "    $0 ${SERVER} <NUMBER_OF_DAYS>"
echo

echo -n "Your certificate fingerprint is: "
openssl x509 -sha256 -noout -fingerprint -in ${SERVER}.pem | sed -e 's/^.*=//;s/://g;y/ABCDEF/abcdef/'

echo
exit 0

