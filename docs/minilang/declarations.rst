Declarations
============

All variables must be declared in *Minilang* using :mini:`var`. Variables are visible within their scope and any nested scopes, including nested functions, unless they are shadowed by another declaration. Before assignment, variables have the value :mini:`nil`. Optionally, a variable can be initialized when it is declared. Note that variables can be referenced within their scope before their declaration, but they will have the value :mini:`nil` before their initialization.

.. code-block:: mini

   print('Y = {Y}\n') -- Y is nil here
   
   var Y := 1 + 2
   
   print('Y = {Y}\n') -- Y is 3 here
   
   var X
   
   do
      X := 1 -- Sets X in surrounding scope
   end
   
   print('X = {X}\n')
   
   do
      var X -- Shadows declaration of X 
      X := 2 -- Assigns to X in the previous line
      print('X = {X}\n')
   end
   
   print('X = {X}\n')

::

   Y =
   Y = 3 
   X = 1
   X = 2
   X = 1

For convenience, functions can declared using the following syntax:

.. code-block:: mini

   fun add(X, Y) X + Y

This is equivalent to writing

.. code-block:: mini

   var add := fun(X, Y) X + Y

Functions themselves are described in :ref:`minilang/functions`.
