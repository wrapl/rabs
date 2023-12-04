.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

symbol
======

.. rst-class:: mini-api

:mini:`type symbol < target`
   A symbol target represents a dynamically scoped binding.
   They can be bound to any other value,  and are considered changed if their bound value changes.


:mini:`fun symbol(Name: string): symbol`
   Returns the symbol with name :mini:`Name`.


