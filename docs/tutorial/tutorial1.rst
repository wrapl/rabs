Tutorial 1
==========

In this tutorial, we will create a Rabs script that updates a generated file whenever a corresponding input file is modified.

Hello world
-----------

Create a new directory called :file:`tutorial` and inside it create a file called :file:`build.rabs` with the following content:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")

Run ``rabs`` in the directory, which will give the following output (with `/tutorial` replaced by the full path of your directory):

.. code-block:: console

   $ rabs
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 1
   Hello world!

Note that ``rabs`` automatically loads and runs the :file:`build.rabs` file. The first line is a comment since it starts with :mini:`:<`. Although comments are generally ignored by ``rabs``, a :mini:`:< ROOT >:` comment on the first line of a :file:`build.rabs` serves as a special marker. 

.. note::

   The :mini:`:< ROOT >:` line is needed to tell ``rabs`` that this is the root directory of our project. Without it, ``rabs`` will search the parent directories until it finds a :file:`build.rabs` file that does start with :mini:`:< ROOT >:`. If we omit this line and do not have a suitable :file:`build.rabs` in any parent directory, we get an error:

   .. code-block:: console

      $ rabs
      Looking for library path at /usr/lib/rabs
      Error: could not find project root

After this, the :file:`build.rabs` contains one other line: :mini:`print("Hello world!\n")` which predictably causes ``rabs`` to print :mini:`"Hello world!"` to the console.

Build Iterations
----------------

If we run ``rabs`` again, we get almost the same output:

.. code-block:: console

   $ rabs
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
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
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 3
   Hello world!
   1 / 1 #0 Updated meta:::DEFAULT to iteration 1

Note that even though the build iteration is now 3 (or possibly higher if you ran ``rabs`` a few more times), the :mini:`DEFAULT` target has only been updated to iteration 1. This is because nothing has changed since the first time ``rabs`` was run.

Build Functions
---------------

Update the :file:`build.rabs` file to look as follows:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   DEFAULT => fun() print("Building DEFAULT\n")

This change sets a *build function* for the :mini:`DEFAULT` target to a function that prints out a single string :mini:`"Building DEFAULT"`. Since :mini:`DEFAULT` is a meta target, its build function doesn't need to do anything specific such as creating a file or returning a value.

Running ``rabs -s`` again produces the following output:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
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
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 5
   Hello world!
   1 / 1 #0 Updated meta:::DEFAULT to iteration 4

This time, the build function for :mini:`DEFAULT` is not executed, and the target is not updated again. Like earlier, since no change was made, ``rabs`` does not need to run the build function for :mini:`DEFAULT` again.

Try changing the build function:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   DEFAULT => fun() print("Building DEFAULT again\n")

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
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
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST")
   Test => fun() print("Building TEST\n")
   
   DEFAULT[Test]
   DEFAULT => fun() print("Building DEFAULT again\n")

This defines a new meta target, called :mini:`TEST`. However running ``rabs -s`` will not display `Building TEST` and :mini:`TEST` will not be updated (or even displayed).

In order for :mini:`TEST` to be built, we need to make one more change:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST")
   Test => fun() print("Building TEST\n")
   
   DEFAULT[Test]
   DEFAULT => fun() print("Building DEFAULT again\n")

The line :mini:`DEFAULT[TEST]` adds :mini:`TEST` as a *dependency* of :mini:`DEFAULT`. This causes 2 things:

#. :mini:`TEST` must be built before :mini:`DEFAULT` and
#. whenever :mini:`TEST` changes, :mini:`DEFAULT` will be rebuilt.

Running ``rabs -s`` shows us this in action:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
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
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST")
   Test => fun() print("Building TEST again\n")
   
   DEFAULT[Test]
   DEFAULT => fun() print("Building DEFAULT again\n")

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 12
   Hello world!
   Building TEST again
   1 / 2 #0 Updated meta:::TEST to iteration 12
      Updating due to meta:::TEST
   Building DEFAULT again
   2 / 2 #0 Updated meta:::DEFAULT to iteration 12

.. note::

   Some targets (e.g. file targets), are considered unchanged even if their build functions was run in an iteration. This happens if the contents / value of a target has not changed despite changes to its build function or dependencies. Since meta targets have no contents or value, they are always considered changed if their build function or any of their dependencies change.

Shorter Syntax
--------------

Our current script describes build functions (using :mini:`Target => Function`) and dependencies (using :mini:`Target[Dependency]`). Both of these operations return the target itself, so we can combine them on one line:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST") => fun() print("Building TEST again\n")
   
   DEFAULT[Test] => fun() print("Building DEFAULT again\n")

File Targets
------------

Now that we can create and update meta targets, it's time to move on to the most useful type of target in ``rabs``, *file* targets. These correspond to files (or directories) on disk. As such, they have contents, which are read when checking if a file has changed.

Add a few more lines to :file:`build.rabs`:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST") => fun() print("Building TEST again\n")
     
   DEFAULT[Test] => fun() print("Building DEFAULT again\n")
   
   var Test2 := file("test.txt") => fun(Target) do
      var File := Target:open("w")
      File:write("Hello world!\n")
      File:close
   end
   
   DEFAULT[Test2]

Running ``rabs -s`` again creates the file :file:`test.txt` with the expected content:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 13
   Hello world!
   1 / 3 #0 Updated file:test.txt to iteration 13
   2 / 3 #0 Updated meta:::TEST to iteration 12
      Updating due to file:test.txt
   Building DEFAULT again
   3 / 3 #0 Updated meta:::DEFAULT to iteration 13
   $ ls
   build.rabs  build.rabs.db/  test.txt
   $ cat test.txt
   Hello world!
   
Notice that in this example, we added :mini:`Test2` as a dependency of :mini:`DEFAULT` on its own line. We could include in the same line as the :mini:`Test` dependency as below:

.. code-block:: mini
   :linenos:

   :< ROOT >:
   
   print("Hello world!\n")
   
   var Test := meta("TEST") => fun() print("Building TEST again\n")
     
   var Test2 := file("test.txt") => fun(Target) do
      var File := Target:open("w")
      File:write("Hello world!\n")
      File:close
   end
   
   DEFAULT[Test, Test2] => fun() print("Building DEFAULT again\n")

In this example, the :file:`test.txt` target has no dependencies so it will only be rebuilt if we change its build function, or if the file itself is deleted fromt the disk:

.. code-block:: console

   $ rm test.txt
   $ cat test.txt
   cat: test.txt: No such file or directory
   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 14
   Hello world!
   1 / 3 #0 Updated file:test.txt to iteration 13
   2 / 3 #0 Updated meta:::TEST to iteration 12
   3 / 3 #0 Updated meta:::DEFAULT to iteration 13
   $ cat test.txt
   Hello world!

Here we get to see how file targets are checked for changes. Despite rebuilding :file:`test.txt`, its updated iteration was not increased since its contents did not change since that last build. Like the last build iteration and information about build functions, this information is stored in the :file:`build.rabs.db` directory. Finally, `rabs` does not rebuild :mini:`DEFAULT` despite its dependency on :file:`test.txt`.

Changing Dependencies
---------------------

Lets add two more targets to the :file:`build.rabs` script:

.. code-block:: mini
   :linenos:

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
   
   DEFAULT[Output]

We are introducing two new features here, using an external file target and calling a shell command. Running `rabs` gives us an error:

.. code-block:: console

   $ rabs -s
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 24
   Hello world!
   Error: rule failed to build: /tutorial/input.txt

`rabs` needs to check the file :file:`input.txt` before it can build :file:`output.txt`, but this file does not exist and it has no build function. So `rabs` complains that the (:mini:`nil`) build function for :file:`input.txt` failed to build the file. This error will also occur for any file target which has a build function if that build function fails to build the expected file.

Create :file:`input.txt` with some text, and run `rabs`. This time add the extra option ``-c`` when running `rabs`.

.. code-block:: console

   $ echo 1 > input.txt
   $ rabs -s -c
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 25
   Hello world!
   1 / 5 #0 Updated file:input.txt to iteration 25
   2 / 5 #0 Updated file:test.txt to iteration 13
      Updating due to file:input.txt
   /tutorial: cp /tutorial/input.txt /tutorial/output.txt 
      0.000101 seconds.
   3 / 5 #0 Updated file:output.txt to iteration 25
   4 / 5 #0 Updated meta:::TEST to iteration 12
      Updating due to file:output.txt
   Building DEFAULT again
   5 / 5 #0 Updated meta:::DEFAULT to iteration 25
   $ cat output.txt
   1

The extra ``-c`` option shows us any shell commands that `rabs` runs, along with the time taken. In this case `rabs` uses `cp` to copy :file:`input.txt` to :file:`output.txt` (`rabs` has its own built in functions for copying files, we used `cp` here as an example).

If we change the contents of :file:`input.txt`, `rabs` will do its job and rebuild :file:`output.txt`:

.. code-block:: console

   $ echo 2 > input.txt
   $ rabs -s -c
   Looking for library path at /usr/lib/rabs
   RootPath = /tutorial
   Building in /tutorial
   Rabs version = 2.5.4
   Build iteration = 28
   Hello world!
   1 / 5 #0 Updated file:input.txt to iteration 28
   2 / 5 #0 Updated file:test.txt to iteration 13
      Updating due to file:input.txt
   /tutorial: cp /tutorial/input.txt /tutorial/output.txt 
      0.000101 seconds.
   3 / 5 #0 Updated file:output.txt to iteration 28
   4 / 5 #0 Updated meta:::TEST to iteration 12
      Updating due to file:output.txt
   Building DEFAULT again
   5 / 5 #0 Updated meta:::DEFAULT to iteration 28
   $ cat output.txt
   2

Now we know how to create and use meta targets, file targets, build functions and shell commands, we can move on to more advanced functionality in :doc:`/tutorial/tutorial2`.
