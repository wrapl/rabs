#!/bin/bash

minilang minilang/src/document.mini docs/library src/*.c
cd docs && sphinx-build . ../html/ && cd ..
