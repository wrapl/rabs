.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

expr
====

.. rst-class:: mini-api

:mini:`type expr < target`
   An expression target represents the a Minilang value that needs to be recomputed whenever other targets change.
   The value of an expression target is the return value of its build function.


:mini:`fun expr(Name: string, BuildFn?: any): expr`
   Returns a new expression target with :mini:`Name`.


