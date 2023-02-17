#!/bin/sh

V=`./bin/rabs -V`
echo Updating Rabs v$V

dch -v $V Updates.
EMAIL="rajamukherji@gmail.com" debuild -us -uc -d

