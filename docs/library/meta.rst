.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

meta
====

.. rst-class:: mini-api

:mini:`type meta < target`
   A meta target represents a target with no other properties other than a build function and dependencies.


:mini:`fun meta(Name: string, BuildFn?: any): meta`
   Returns a new meta target in the current context with name :mini:`Name`.


