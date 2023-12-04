Rabs
====

Overview
--------

Rabs (Raja's Attempt at a Build System) is a general purpose build system, designed for fast and consistent incremental rebuilds. See here to get started: :doc:`/quickstart`.

Features
--------

Rabs provides the following features:

* Support for large projects spanning multiple directories with arbitrary nesting.
* Allows build and source directories to be overlayed so that new files are automatically created in the build directory while input files can be located in either.
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

   :< ROOT >:
   
   var Src := file("src")
   var Bin := file("bin"):mkdir
   var Obj := file("obj"):mkdir
   
   var TestC := Src / "test.c"
   var TestH := Src / "test.h"
   var TestO := Obj / "test.o"
   
   TestO[TestC, TestH] => fun() do
      execute('gcc -c -o{TestO} -I{Src} {TestC}')
   end
   
   var Test := Bin / "test"
   
   Test[TestO] => fun() do
      execute('gcc -o{Test} {TestO}')
   end
   
   DEFAULT[Test]

Rabs is designed to build (or rebuild) a set of targets as necessary, by considering dependencies and changes to the contents / values of those dependencies.
For a general overview of targets in Rabs, see here: :doc:`/targets`. 

Rabs provides several types of targets: 

* :doc:`/targets/files`
* :doc:`/targets/symbols`
* :doc:`/targets/meta`
* :doc:`/targets/expressions`
* :doc:`/targets/scans`

Rabs build scripts are written using `Minilang <https://minilang.readthedocs.io>`_. Nearly all *Minilang* features are available, with the exception of the following:

* The preemptive scheduler is disabled. Instead, *Rabs* provides its own support for parallel builds.
* The ``math``, ``array``, ``json`` and ``xml`` modules are not included.

.. toctree::
   :maxdepth: 2
   :caption: Contents:
   
   /quickstart
   /tutorial
   /usage
   /reference
   /library

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
