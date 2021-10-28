Expression Targets
==================

An expression target represents a computable value. Expression targets can be used for capturing the output of config generation tools (e.g. :file:`pkg-config`) preventing the need to run them unnecessarily at each build.

To create an expression target, call the built-in :mini:`expr(Name)` function with a name that is unique to the current context. Calling :mini:`expr(Name)` with a name that already of an existing expr target in the current context will simply return the existing target.

.. code-block:: mini

   var ExprTarget := expr("GTK_CFLAGS")
   
   ExprTarget => fun(Target) do
      shell("pkg-config --cflags gtk+-3.0"):trim
   end
   

Build Function
--------------

When an expression target is used as a value (e.g. in a shell command), it is replaced by the value returned by the target's build function. Most common types of value are supported, booleans, numbers, strings, lists and maps are the most important.

Update Detection
----------------

An expression target is considered updated when the value computed by its build function has changed (using the Minilang hash method).
