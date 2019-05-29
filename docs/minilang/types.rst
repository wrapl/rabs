Types
=====

*Minilang* provides a number of built-in types.

Nil
---

The special built in value ``nil`` denotes the absence of any other value. Every variable has the value ``nil`` before they are assigned any other value. Likewise, function parameters default to ``nil`` if a function is called with too few arguments.

.. note::

   *Minilang* has no boolean values, such as ``true`` or ``false``. Instead, conditional and looping statements treat ``nil`` as false and *any* other value as true.  

.. code-block:: mini

   -- Nil
   nil
   
   if nil then
      print("This will not be seen!")
   end
   
   if 0 then
      print("This will be seen!")
   end

.. _comparisons:

In *Minilang*, comparison operators such as ``=``, ``>=``, etc, all return the second argument if the comparison is true, and ``nil`` if it is not.

.. code-block:: mini

   1 < 2 -- returns 2
   1 > 2 -- returns nil 

Numbers
-------

Numbers in *Minilang* are either integers (whole numbers) or reals (decimals / floating point numbers).

Integers can be written in standard decimal notation. Reals can be written in standard decimal notation, with either ``e`` or ``E`` to denote an exponent in scientific notation. If a number contains either a decimal point ``.`` or an exponent, then it will be read as a real number, otherwise it will be read as an integer.

They support the standard arithmetic operations, comparison operations and conversion to or from strings.

.. code-block:: mini

   -- Integers
   10
   127
   -1
   
   --Reals
   1.234
   10.
   0.78e-12

Strings
-------

Strings can be written in two ways:

Regular strings are written between double quotes ``"``, and contain regular characters. Special characters such as line breaks, tabs or ANSI escape sequences can be written using an escape sequence ``\n``, ``\t``, etc.

Complex strings are written between single quotes ``'`` and can contain the same characters and escape sequences as regular strings. In addition, they can contain embedded expressions between braces ``{`` and ``}``. At runtime, the expressions in braces are evaluated and converted to strings. To include a left brace ``{`` in a complex string, escape it  ``\{``.

.. code-block:: mini

   -- Regular strings
   "Hello world!"
   "This has a new line\n", "\t"
   
   -- Complex strings
   'The value of x is \'{x}\''
   'L:length = {L:length}\n'
   
Regular Expressions
-------------------

Regular expressions can be written as ``r"expression"``, where *expression* is a POSIX compatible regular expression.

.. code-block:: mini

   -- Regular expressions
   r"[0-9]+/[0-9]+/[0-9]+"

Lists
-----

Lists are extendable ordered collections of values, and are created using square brackets, ``[`` and ``]``. A list can contain any value, including other lists.

Maps
----

Maps are created using braces ``{`` and ``}``.
