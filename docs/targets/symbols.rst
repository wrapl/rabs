Symbol Targets
==============

A symbol target represents a context-specific value binding.

Symbol targets are created automatically whenever an undeclared identifier is used. They can be created explicitly using the :mini:`symbol(Name)` function, in cases where the name is created programmatically.

.. code-block:: mini

   var SymbolTarget := SYMBOL

When symbols are used in any code, they are resolved in the current context. Thus the same symbol in the same function may resolve to different values depending on the current context when the function is called. This is used to have context specific flags or settings in general purpose build functions. See :doc:`/contexts` for an example.

Using a symbol while building a target also adds that symbol as a dependency to that target. In some cases, it may be necessary to explicitly add a symbol as a dependency to a target even if it is not used within the target's build function. Since using a symbol in any code automatically resolves to the symbols value, the symbol name must be added as a string instead.

.. code-block:: mini

   var FileTarget := file("test.o")
   
   FileTarget["SYMBOL"]

Build Function
--------------

Symbols should not have build functions.

Update Detection
----------------

A symbol is considered updated if its value changes. Note that if the value of a symbol is another target, the update status of that target does not affect the symbol's update status.
