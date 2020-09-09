Tutorial 2
==========

Recap
-----

So far, we have created a root :file:`build.rabs` script, and defined some *meta* and *file* targets. We added some build functions and dependencies. We've also seen how `rabs` builds or rebuilds targets when necessary, and skips targets otherwise. In this tutorial, we will introduce some more target types in `rabs`.

Nested Directories
------------------

So far, we have only used one :file:`build.rabs` script in a single directory. Larger projects are usually organized in multiple, nested directories. ``rabs`` provides a number of builtin functions and features for dealing with complex project structures, some of which we will cover here.

To start, create a new directory called :file:`src` within the :file:`tutorial` directory created in the previous tutorial. This is where we will put source code files for our tutorial project. Inside the new :file:`src`, create another file called :file:`build.rabs` with the following content:

.. code-block:: mini
   :linenos:
   
   print("Hello from a subdirectory!\n")
   
   DEFAULT => fun print("Building DEFAULT in src\n")

Note that this :file:`build.rabs` file does not need to start with :mini:`:< ROOT >:`.

.. note::

   Nested :file:`build.rabs` files may also start with :mini:`:< ROOT >:` without problems. In fact, this is useful for nested projects (using a git submodule for example) which can be built both independently or as part of a larger project.


Finally, add one more line to the :file:`build.rabs` file in the root :file:`tutorial` directory:

.. code-block:: mini
   :linenos:
   :emphasize-lines: 20

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST") => fun() print("Building TEST again\n")
     
   var Test2 := file("test.txt") => fun(Target) do
      var File := Target:open("w")
      File:write("Hello world!\n")
      File:close
   end
   
   DEFAULT[Test, Test2] => fun() print("Building DEFAULT again\n")
   
   var Input := file("input.txt")
   var Output := file("output.txt")[Input] => fun(Target) do
      execute('cp {Input} {Output}')
   end
   
   subdir("src")
   
   DEFAULT[Output]

The final directory structure should look like this:

.. folders::
   
   - build.rabs
   + src
      - build.rabs

Run ``rabs`` as before (in the root :file:`tutorial` directory).

.. code-block:: console

   $ rabs -s -c
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.11.0
   Build iteration = 33
   Hello world!
   Hello from a subdirectory!
   1 / 6 #0 Updated file:input.txt to iteration 1
   2 / 6 #0 Updated meta:::TEST to iteration 1
   3 / 6 #0 Updated file:output.txt to iteration 1
   4 / 6 #0 Updated file:test.txt to iteration 1
   Building DEFAULT in src
   5 / 6 #0 Updated meta:/src::DEFAULT to iteration 33
      Updating due to meta:/src::DEFAULT
   Building DEFAULT again
   6 / 6 #0 Updated meta:::DEFAULT to iteration 33

When ``rabs`` runs the :mini:`subdir("src")` function, it loads and runs the file :file:`src/build.rabs`. It also automatically creates a new meta target called :mini:`DEFAULT` specific to the :file:`src` directory, and adds it as a dependency of :mini:`DEFAULT` in the root directory.

Contexts
--------

Now we have multiple targets, defined in multiple :file:`build.rabs` files located in different directories. In most programming languages, definitions in different files are kept seperate except through module imports or similar mechanisms. Since ``rabs`` is designed to simplify building large nested projects, definitions in :file:`build.rabs` files are automatically made available to :file:`build.rabs` in nested directories. 

Similarly, :file:`build.rabs` files in nested directories can affect (add or update) definitions in their parent directories. As a result, ``rabs`` defines the concept of a *context* when running code. Typically, a context is associated with the directory of the current :file:`build.rabs` file. Each context has a single parent context, again typically the context of the parent directory. It is however possible to create multiple contexts within a single directory, which we will see in a later tutorial.    



Symbols Targets
---------------

Build instructions are often parameterised:

#. Various flags or options are passed to external programs such as compilers.
#. The same project can be built in different configurations by passing options to the build program (in this case ``rabs``).

``rabs`` provides *symbol* targets to store and retrieve values containing parameters, flags, options, etc. Symbols are created by assigning to an identifier which has **not** been declared as a variable.

.. code-block:: mini
   :linenos:
   
   :< ROOT >:

   var Variable := "value"
   
   Symbol := "value"
   
   print('Variable = ', Variable, '\n')
   print('Symbol = ', Symbol, '\n')

Here, :mini:`Variable` is a normal variable and :mini:`Symbol` is a *symbol*. Both have been assigned the same value, :mini:`"value"` and can be used in code by their identifiers. Although symbols are similar to variables in many ways, they also have a number of extra properties:

* Symbols are tracked automatically as dependencies when used in a build function. If the value of a symbol is changed during a subsequent build, ``rabs`` will rebuild any targets whose build functions used the symbol.
* Symbols are inherited by contexts, and can be overridden (redefined) in a context without affecting the parent context. 