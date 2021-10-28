expr
====

:mini:`type exprtarget < target`
   An expression target represents the a Minilang value that needs to be recomputed whenever other targets change.

   The value of an expression target is the return value of its build function.


:mini:`meth string(Target: exprtarget): string`
   Converts the value of an expression target to a string, calling its build function first if necessary.


:mini:`fun expr(Name: string): exprtarget`
   Returns a new expression target with :mini:`Name`.


