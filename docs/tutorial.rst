Tutorial
========

In this tutorial, we will create a Rabs script that updates a generated file whenever a corresponding input file is modified.

Hello world
-----------

Create a new directory and inside it create a file called :file:`build.rabs` with the following content.

.. code-block:: mini

   -- ROOT --
   
   print("Hello world!\n")

Run ``rabs`` in the directory, which will give the following output (with `<path>` replaced by the actual path of your directory):

.. code-block:: console

   $ rabs
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 1
   Hello world!

Note that ``rabs`` automatically loads and runs the :file:`build.rabs` file. The first line is a comment since it starts with :mini:`--`. Although comments are generally ignored by ``rabs``, a :mini:`-- ROOT --` comment on the first line of a :file:`build.rabs` serves as a special marker. 

.. note::

   The :mini:`-- ROOT --` line is needed to tell ``rabs`` that this is the root directory of our project. Without it, ``rabs`` will search the parent directories until it finds a :file:`build.rabs` file that does start with :mini:`-- ROOT --`. If we omit this line and do not have a suitable :file:`build.rabs` in any parent directory, we get an error:

   .. code-block:: console

      $ rabs
      Looking for library path at /usr/lib/rabs
      Error: could not find project root

After this, the :file:`build.rabs` contains one other line: :mini:`print("Hello world!\n")` which predictably causes ``rabs`` to print `Hello world!` to the console.

Build Iterations
----------------

If we run ``rabs`` again, we get almost the same output:

.. code-block:: console

   $ rabs
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 2
   Hello world!

The only difference is the *build iteration* has increased from 1 to 2. This means that ``rabs`` knows that this is the second time it has been run in this directory. ``rabs`` does this by storing information each time it is run in a directory called :file:`build.rabs.db`, creating this directory if it does not yet exist. If we list the files in the directory, we see this new directory.

.. code-block:: console

	$ ls
	build.rabs build.rabs.db/  

The Default Target
------------------

So far, each time we run ``rabs``, the same output is produced with the exception of the build iteration. However, the main purpose of ``rabs`` is as an incremental build system, i.e. it should run code as needed to update a project, and not run unnecessary code. To do this, we define *targets* that ``rabs`` will update when necessary. There are many types of target, but the simplest is a *meta* target, which is defined only by its name. ``rabs`` automatically defines a default target, accessible as :mini:`DEFAULT`, which is updated whenever ``rabs`` is run.

If we run ``rabs`` with the additional argument `-s`, we can see how it checks and updates the :mini:`DEFAULT` target.

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 3
   Hello world!
   1 / 1 #0 Updated meta:::DEFAULT to iteration 1

Note that even though the build iteration is now 3 (or possibly higher if you ran ``rabs`` a few more times), the :mini:`DEFAULT` target has only been updated to iteration 1. This is because nothing has changed since the first time ``rabs`` was run.

Build Functions
---------------

Update the :file:`build.rabs` file to look as follows:

.. code-block:: mini

   -- ROOT --
   
   print("Hello world!\n")
   
   DEFAULT => fun() print("Building DEFAULT\n")

This change sets a *build function* for the :mini:`DEFAULT` target to a function that prints out a single string `Building DEFAULT`. Since :mini:`DEFAULT` is a meta target, its build function doesn't need to do anything specific such as creating a file or returning a value.

Running ``rabs -s`` again produces the following output:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 4
   Hello world!
   Building DEFAULT
   1 / 1 #0 Updated meta:::DEFAULT to iteration 4

We see the message `Building DEFAULT` and the :mini:`DEFAULT` target has been updated to match the build iteration.

If we try running ``rabs -s`` again, we'll get different output:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 5
   Hello world!
   1 / 1 #0 Updated meta:::DEFAULT to iteration 4

This time, the build function for :mini:`DEFAULT` is not executed, and the target is not updated again. Like earlier, since no change was made, ``rabs`` does not need to run the build function for :mini:`DEFAULT` again.

Try changing the build function:

.. code-block:: mini

   -- ROOT --
   
   print("Hello world!\n")
   
   DEFAULT => fun() print("Building DEFAULT again\n")

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 6
   Hello world!
   Building DEFAULT again
   1 / 1 #0 Updated meta:::DEFAULT to iteration 6

``rabs`` detects that the build function has changed and runs it again, as expected. Running ``rabs`` again after this will be similar to earlier.

.. note::

  ``rabs`` detects any functional changes in a build function such as added or removed code or different constant values. Comments and formatting do **not** count as changes. 

Targets and Dependencies
------------------------

`rabs` predefines the :mini:`DEFAULT` target, but other targets can be created in the :file:`build.rabs` script. Change :file:`build.rabs` to contain the following:

.. code-block:: mini

   -- ROOT ---
   
   print("Hello world!\n")
   
   var TEST := meta("TEST")
   TEST => fun() print("Building TEST\n")
   
   DEFAULT[TEST]
   DEFAULT => fun() print("Building DEFAULT again\n")

This defines a new meta target, called :mini:`TEST`. However running ``rabs -s`` will not display `Building TEST` and :mini:`TEST` will not be updated (or even displayed).

In order for :mini:`TEST` to be built, we need to make one more change:

.. code-block:: mini

   -- ROOT ---
   
   print("Hello world!\n")
   
   var TEST := meta("TEST")
   TEST => fun() print("Building TEST\n")
   
   DEFAULT[TEST]
   DEFAULT => fun() print("Building DEFAULT again\n")

The line :mini:`DEFAULT[TEST]` adds :mini:`TEST` as a *dependency* of :mini:`DEFAULT`. This causes 2 things:

#. :mini:`TEST` must be built before :mini:`DEFAULT` and
#. whenever :mini:`TEST` changes, :mini:`DEFAULT` will be rebuilt.

Running `rabs -s` shows us this in action:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 11
   Hello world!
   Building TEST
   1 / 2 #0 Updated meta:::TEST to iteration 11
      Updating due to meta:::TEST
   Building DEFAULT again
   2 / 2 #0 Updated meta:::DEFAULT to iteration 11

Not only is the build function for :mini:`TEST` executed, the build function for :mini:`DEFAULT` is also executed again. If we change the build function for :mini:`TEST`, both it and :mini:`DEFAULT` will be rebuilt.

.. code-block:: mini

   -- ROOT ---
   
   print("Hello world!\n")
   
   var TEST := meta("TEST")
   TEST => fun() print("Building TEST again\n")
   
   DEFAULT[TEST]
   DEFAULT => fun() print("Building DEFAULT again\n")

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = <path>
   Building in <path>
   Rabs version = 2.5.4
   Build iteration = 12
   Hello world!
   Building TEST again
   1 / 2 #0 Updated meta:::TEST to iteration 12
      Updating due to meta:::TEST
   Building DEFAULT again
   2 / 2 #0 Updated meta:::DEFAULT to iteration 12

.. note::

   Some targets (e.g. file targets), may be considered unchanged even if their build functions was run in an iteration. This will happen if the contents / value of a target has not changed despite a changes to its build function or dependencies. Meta targets are always considered updated if their build function or any of their dependencies change (since they have no other contents / value).

