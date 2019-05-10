.. Rabs documentation master file, created by
   sphinx-quickstart on Thu May  9 07:06:34 2019.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to Rabs's documentation!
================================

Overview
--------

Rabs is a general purpose build system, designed for fast and consistent incremental rebuilds.

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

   PLATFORM := defined("PLATFORM") or shell("uname"):trim
   OS := defined("OS")
   DEBUG := defined("DEBUG")
   
   CFLAGS := []
   LDFLAGS := []
   PREBUILDS := []
   
   c_compile := fun(Source, Object) do
      execute('gcc -c {CFLAGS} -o{Object} {Source}')
   end
   
   c_includes := fun(Target) do
      var Files := []
      var Lines := shell('gcc -c {CFLAGS} -M -MG {Target:source}')
      var Files := Lines:trim:replace(r"\\\n ", "") / r"[^\\]( )"
      Files:pop
      for File in Files do
         File := file(File:replace(r"\\ ", " "))
      end
      return Files
   end
   
   var SourceTypes := {
      "c" is [c_includes, c_compile]
   }
   
   c_program := fun(Executable, Objects, Libraries) do
      Objects := Objects or []
      Libraries := Libraries or []
      var Sources := []
      for Object in Objects do
         for Extension, Functions in SourceTypes do
            var Source := Object % Extension
            if Source:exists then
               Sources:put(Source)
               var Scan := Source:scan("INCLUDES")[PREBUILDS] => Functions[1]
               Object[Source, Scan] => (Functions[2] !! [Source])
               exit
            end
         end
      end
      Executable[Objects, Libraries] => fun(Executable) do
         execute('gcc', '-o', Executable, Objects, Libraries, LDFLAGS)
         DEBUG or execute('strip', Executable)
      end
      DEFAULT[Executable]
   end

Rabs is designed to build (or rebuild) a set of targets as necessary, by considering dependencies and changes to the contents / values of those dependencies.
For a general overview of targets in Rabs, see here: :doc:`targets/targets`. 

Rabs provides several types of targets: 

* :doc:`targets/files`
* :doc:`targets/symbols`
* :doc:`targets/meta`
* :doc:`targets/expressions`
* :doc:`targets/scans`


Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
