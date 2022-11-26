Usage
=====

.. code-block:: console

   $ rabs <options>

where ``<options>`` is any combination of the following:

``-h``
   Show help and exit.
``-V``
   Show version and exit.
``-D`` *NAME*\ ``=``\ *VALUE*
   Add a define for *NAME* with value *VALUE*. The ``=``\ *VALUE* may be omitted in which case ``1`` is used.
``-c``
   Show command lines as they as are executed.
``-s``
   Show status updates as targets are built.
``-b``
   Show a progress bar as targets are built.
``-E`` *FILENAME*
   Capture ``stderr`` output from commands to *FILENAME*.
``-p`` *COUNT*
   Execute up to *COUNT* commands in parallel.
``-F`` *FILENAME*
   Use *FILENAME* instead of :file:`build.rabs` as the build file name.
``-G``
   Generate a dependancy graph in :file:`dependencies.dot`.
``-i``
   Run in interactive mode showing a console instead of building any targets.
``-d``
   Show activity in each thread (for debugging slow builds).