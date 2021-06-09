#!/bin/sh

V=`./bin/rabs -V`
echo Releasing Rabs v$V

PR=`gh pr create -f`
echo Created pull request $PR
sleep 5
gh pr merge $PR -m --delete-branch=false
gh release create v$V -t "v$V" -F changelog.md
#gh release upload v$V ../rabs_${V}_amd64.deb
