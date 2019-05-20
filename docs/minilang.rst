Minilang
========

.. warning::
   Minilang uses the `Hans-Boehm conservative garbage collector <https://github.com/ivmai/bdwgc>`_. This simplifies memory management in most cases, but may not be compatible with all use cases. In the future, the option to use a different form of memory management may be added to Minilang. 

Introduction
------------

*Minilang* is a small imperative language, designed to be embedded into programs written in *C*.
It has the following goals / features:

**Minimal dependencies**
   *Minilang* has no dependencies other than the Hans Boehm garbage collector, and standard *C* libraries.

**Full Closures**
   Functions in *Minilang* automatically capture their environment, creating closures at runtime.
   Closures can be passed around as first-class values, which are used to build targets in *Rabs*. 

**Dynamic scoping**
   The *Minilang* interpreter provides a callback for identifier resolution, allowing programs to provide dynamic scoping.
   This is used within *Rabs* to provide dynamic symbol resolution.

**Easy to extend**
   It is easy to create new functions in *C* to use in *Minilang*.

Sample
------

.. code-block:: mini

   var L := [1, 2, 3, 4, 5]
 
   for X in L do
      print('X = {X}\n')
   end

Syntax
------

*Minilang* uses a simple syntax, similar to *Pascal* or *Lua*.