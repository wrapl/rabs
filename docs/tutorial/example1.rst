Simple Examples
===============

Single Folder Example
---------------------

Suppose we have a single *C* source file :file:`hello.c`. To build a program :file:`hello` from this file, we can use the following :file:`build.rabs` file.

.. code-block:: mini

   -- ROOT --
   
   file("hello.o")[file("hello.c")] => fun() do
      execute('gcc -c -o{file("hello.o")} {file("hello.c")}')
   end
   
   file("hello")[file("hello.o")] => fun() do
      execute('gcc -o{file("hello")} {file("hello.o")}')
   end
   
   DEFAULT[file("hello")]

We place the :file:`build.rabs` in the same folder, so it looks as follows.

.. folders::

   - build.rabs
   - hello.c

Running ``rabs -c`` in the folder, gives the following output:

.. code-block:: console

   $ rabs -c
   output

.. folders::

   - build.rabs
   - build.rabs.db
   - hello
   - hello.c
   - hello.o


