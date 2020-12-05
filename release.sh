#!/bin/sh

V=`./bin/rabs -V`
echo Releasing Rabs v$V

gh pr create -f
gh pr merge -m
gh release create v$V -t v$V
gh release upload v$V ../rabs_${V}_amd64.deb
