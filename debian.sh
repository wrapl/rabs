#!/bin/sh

V=`./bin/rabs -V`
echo Updating Rabs v$V

dch -v $V Updates.
debuild -us -uc -d

