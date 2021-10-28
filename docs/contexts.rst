Contexts
========

*Contexts* are used in ``rabs`` to provide dynamic hierarchical scoping for symbols. Symbols defined in a context are visible in any child contexts, and can be overridden without affecting the parent context. Each context has an associated path for identification which is also used as a prefix for targets created within the context.

The root :file:`build.rabs` file is executed in the root context. New contexts are created using :mini:`subdir()` or :mini:`scope()`, using the current context as the parent.

.. code-block:: mini

   subdir("directory") :> Will load directory/build.rabs in child context
   
   scope("test";) do
      :> Inside child context
   end

Build Contexts
--------------
 
Some targets (e.g. file targets) can be anywhere in the project and thus cannot be bound to a specific context. This means that flags or other settings (e.g. compilation flags) cannot be bound to targets.
 
However, the build function for each target should only be set once. ``rabs`` remembers the context where the build function for each target was set and uses the same context when performing the build.

Example
~~~~~~~

.. code-block:: mini

   :< ROOT >:
   
   CFLAGS := ["-pipe"]
   
   fun compile_c(Target) do
      let Source := Target % "c"
      execute("cc", CFLAGS, Source, "-o", Target)
   end
   
   let Objects := [
      file("main.o"),
      file("test.o")
   ]
   
   file("main.o") => compile_c
   
   scope("test";) do
      CFLAGS := old + ["-O3"]
      file("test.o") => compile_c
   end
   
   DEFAULT[Objects]

In this example, :file:`main.o` will be compiled using just ``-pipe`` for *CFLAGS* but :file:`test.o` will be compiled using ``-pipe -O3``.

Default Targets
---------------

Each directory context defines a :mini:`DEFAULT` :doc:`meta target </targets/meta>`. If a directory context is not the root context then its :mini:`DEFAULT` target is automatically added as a dependency of the parent context's :mini:`DEFAULT` target.
