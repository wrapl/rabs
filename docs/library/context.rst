.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

context
=======

.. _type-context:

:mini:`type context < any`
   A build context.


:mini:`meth (Context: context) . (Name: string): symbol`
   Returns the symbol :mini:`Name` resolved in :mini:`Context`.


:mini:`meth (Context: context) / (Name: string): context | nil`
   Returns the directory-based subcontext of :mini:`Context` named :mini:`Name`,  or :mini:`nil` if no such context has been defined.


:mini:`meth (Context: context) :: (Name: string): symbol`
   Returns the symbol :mini:`Name` resolved in :mini:`Context`.


:mini:`meth (Context: context):exports: list`
   Returns a list of symbols defined in :mini:`Context`.


:mini:`meth (Context: context):in(Function: function): any`
   Calls :mini:`Function()` in the context of :mini:`Context`.


:mini:`meth (Context: context):name: string`
   Returns the name of :mini:`Context`.


:mini:`meth (Context: context):parent: context | nil`
   Returns the parent context of :mini:`Context`,  or :mini:`nil` if :mini:`Context` is the root context for the build.


:mini:`meth (Context: context):path: string`
   Returns the path of :mini:`Context`.


:mini:`meth (Context: context) @ (Name: string): context | nil`
   Returns the scope-based subcontext of :mini:`Context` named :mini:`Name`,  or :mini:`nil` if no such context has been defined.


