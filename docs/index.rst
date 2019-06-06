.. Rabs documentation master file, created by
   sphinx-quickstart on Thu May  9 07:06:34 2019.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to Rabs's documentation!
================================

Overview
--------

Rabs is a general purpose build system, designed for fast and consistent incremental rebuilds. See here to get started: :doc:`/quickstart`.

Features
--------

Rabs provides the following features:

* Support for large projects spanning multiple directories with arbitrary nesting.
* A free-form imperative programming language for specifying targets, dependencies and build functions.
* Functional changes to build functions will trigger a rebuild. Comments and formatting do not count as functional changes.
* File targets are checked for changes using SHA-256 checksums to prevent unnecessary rebuilds.
* Support for dynamic dependency sets.
* Uses dynamic scoping so that build functions can be defined in one place and used with different settings as required.

On the other hand, Rabs deliberately omits some features found in other build systems:

* No implicit support for any programming languages. Rabs is designed to be completely generic.
* String values must be quoted.
* File names must be quoted (and wrapped in a call to ``file()``).
* No rules. Rabs uses an explicit build function per target approach.

Example
-------

.. code-block:: mini

   -- ROOT --
   
   var SRC := file("src")
   var BIN := file("bin"):mkdir
   var OBJ := file("obj"):mkdir
   
   var TEST_C := SRC / "test.c"
   var TEST_H := SRC / "test.h"
   var TEST_O := OBJ / "test.o"
   
   TEST_O[TEST_C, TEST_H] => fun() do
      execute('gcc -c -o{TEST_O} -I{SRC} {TEST_C}')
   end
   
   var TEST := BIN / "test"
   
   TEST[TEST_O] => fun() do
      execute('gcc -o{TEST} {TEST_O}')
   end
   
   DEFAULT[TEST]

Rabs is designed to build (or rebuild) a set of targets as necessary, by considering dependencies and changes to the contents / values of those dependencies.
For a general overview of targets in Rabs, see here: :doc:`/targets`. 

Rabs provides several types of targets: 

* :doc:`/targets/files`
* :doc:`/targets/symbols`
* :doc:`/targets/meta`
* :doc:`/targets/expressions`
* :doc:`/targets/scans`

Rabs build scripts are written using :doc:`/minilang`.

.. toctree::
   :maxdepth: 2
   :caption: Contents:
   
   /quickstart
   /tutorial
   /reference

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
