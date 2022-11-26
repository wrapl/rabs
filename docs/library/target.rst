.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

target
======

.. rst-class:: mini-api

.. _type-target:

:mini:`type target < any`
   Base type for all targets.


:mini:`meth (Target: target) << (Dependency: any): target`
   Adds a dependency to :mini:`Target`. Equivalent to :mini:`Target[Dependency]`.


:mini:`meth (Target: target) => (Function: any): target`
   Sets the build function for :mini:`Target` to :mini:`Function` and returns :mini:`Target`. The current context is also captured.


:mini:`meth (Target: target)[Dependency: any, ...]: target`
   Adds a number of dependencies to :mini:`Target`.
   * If :mini:`Dependency` is a list then each value is added.
   * If :mini:`Dependency` is a string then a dependency on the corresponding symbol target is added.
   * Otherwise :mini:`Dependency` should be another target.
   Returns :mini:`Target`.


:mini:`meth (Target: target):affects: targetset`
   Returns the set of dependencies of :mini:`Target`.


:mini:`meth (Target: target):build: function | nil`
   Returns the build function of :mini:`Target` if one has been set,  otherwise returns :mini:`nil`.


:mini:`meth (Target: target):build(Function: any): target`
   Sets the build function for :mini:`Target` to :mini:`Function` and returns :mini:`Target`. The current context is also captured.


:mini:`meth (Target: target):depends: targetset`
   Returns the set of dependencies of :mini:`Target`.


:mini:`meth (Target: target):id: string`
   Returns the id of :mini:`Target`.


:mini:`meth (Target: target):priority: integer`
   Returns the computed priority of :mini:`Target`.


:mini:`meth (Target: target):scan(Name: string): scan`
   Returns a new scan target using :mini:`Target` as the base target.


