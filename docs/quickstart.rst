Quickstart
==========

Installation
------------

*Rabs* has been developed mainly on Linux, with some testing on Mac OS (using *Homebrew*) and FreeBSD and Android.

Building from Source
~~~~~~~~~~~~~~~~~~~~

* Requirements
   #. C compiler, mainly tested with *gcc*,
   #. *make* (use *gmake* on FreeBSD),
   #. Hans-Boehm garbage collector, available as libgc or similar in most  repositories.

* Clone repository
   .. code-block:: console
   
      $ git clone --recursive https://github.com/wrapl/rabs
      $ cd rabs
   
* Make and install
   .. code-block:: console
   
      $ make -j4
      $ sudo make install PREFIX=/usr
      
* Test
   .. code-block:: console
   
      $ rabs -V
      2.22.1

Install using Package
~~~~~~~~~~~~~~~~~~~~~

* From AUR (Arch Linux, Manjaro, etc): `AUR Link <https://aur.archlinux.org/packages/rabs/>`_
* Homebrew (Mac OS): ``brew install wrapl/rabs/rabs``
* FreeBSD ``pkg install rabs``


Building a small C program
--------------------------

Suppose we have a single directory project, containing a single source file, :file:`hello.c`.

.. code-block:: c

   #include <stdio.h>
   
   int main(int Argc, char **Argv) {
      printf("Hello world!\n");
      return 0;
   }

To build an executable, :file:`hello` from this file with *Rabs*, we can create the following :file:`build.rabs` file.

.. code-block:: mini

   :< ROOT >:
   
   file("hello.o")[file("hello.c")] => fun() do
      execute('gcc -c -o{file("hello.o")} {file("hello.c")}')
   end
   
   file("hello")[file("hello.o")] => fun() do
      execute('gcc -o{file("hello")} {file("hello.o")}')
   end

Folder structure
----------------

*Rabs* is designed for building large projects that can span several directories with arbitrary nesting. Each directory contains a :file:`build.rabs` file which specifies the targets to build within that directory, the instructions to build those targets (written in `Minilang <https://minilang.readthedocs.io>`_), and any dependencies.

.. folders::
   
   - build.rabs
   + folder 1
      - build.rabs
   + folder 2
      - build.rabs
   + folder 3
      - build.rabs
   - file 1
   - file 2

Each :file:`build.rabs` file introduces a new scope, which allows per-directory configurations. This scope also inherits the scope from the parent directory, allowing common functionality or settings to be defined in the root directory of the project.

*Rabs* can be run from any directory within the project, in which case it will only build targets specified within that directory (or its subdirectories). The :file:`build.rabs` file in the project's root directory must start with special comment line:

.. code-block:: mini
   
   :< ROOT >:

No patterns, only code
----------------------

Unlike many other build systems, *Rabs* does not use patterns to denote dependencies and build functions. Instead, every dependencies and build function must be explicitly created by code. 

For example, a :file:`Makefile` may contain a rule like the following:

.. code-block:: make

   %.o : %.c
      $(CC) -c $(CFLAGS) $< -o $@

which describes how to create an object file (which has extension ``.o``) from a source file (ending in ``.c``). This rule will be used any time a file matching :file:`*.o` is required in the build *and* a file :file:`*.c` is present.

This could be used to build a program:

.. code-block:: make

   program : program.o
      $(CC) $< -o $@

And *make* would automatically apply the pattern above to build :file:`program.o` from :file:`program.c`, assuming :file:`program.c` existed.

The equivalent in a :file:`build.rabs` file looks like:

.. code-block:: mini

   var c_object := fun(Object) do
      var Source := Object % "c"
      Object[Source] => fun() execute(CC, "-c", CFLAGS, Source, "-o", Object)
   end

Note that there are two functions in the above code, one to create the dependency and build rule and the other to perform the actual build.

This could be used to build a program:

.. code-block:: mini

   var Objects := [c_object(file("program.o"))]
   
   file("program")[Objects] => fun(Target) execute(CC, Objects, "-o", Target)

Although this approach may seem more verbose than using patterns, the ability to use functions and looping constructs to create targets means that overall build scripts tend to be shorter. It is also easier to ensure that targets are built with the correct options since the build context is captured when a target's build function is defined.
