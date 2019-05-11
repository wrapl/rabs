File Targets
============

File targets correspond to files or directories in the file system. Each file target has a path which is either absolute or relative to the root directory of the project.

To create a file target, call the built-in ``file`` function with the path to the file:

.. code-block:: mini

   var FileTarget := file("filename.txt")

When called with a relative path, the path of the current context is used as the base. See :doc:`/contexts` for more information.

.. note::

   If a file target is created with an absolute path that lies within the project directory, the path is converted to a relative path. This ensures that the file target still references the correct location even if the project directory is moved in the file system.

Methods
-------

.. code-block:: mini

   FileTarget / Path

Returns a new file target by appending ``Path`` to ``FileTarget``.

.. code-block:: mini

   FileTarget % Extension

Returns a new file target by replacing the extension of ``FileTarget`` with ``Extension``.

.. code-block:: mini

   FileTarget:dir

Returns a new file target pointing to the directory containing ``FileTarget``.

.. code-block:: mini

   FileTarget:dirname

Returns a string containing the path of the directory containing ``FileTarget``. This can differ from calling ``FileTarget:dir`` due to virtual mounts. See :doc:`/vmounts`.

.. code-block:: mini

   FileTarget:basename

Returns a string containing the base name of ``FileTarget``.

.. code-block:: mini

   FileTarget:extension

Returns a string containing the extension of ``FileTarget``.

.. code-block:: mini

   FileTarget:map(Source, Dest)

Returns a new file target found by computing a relative path from ``Source`` to ``FileTarget`` and appending it to ``Dest``.

.. code-block:: mini

   FileTarget1 - FileTarget2

Returns the relative path from ``FileTarget2`` to ``FileTarget1``. If ``FileTarget1`` is not contained somewhere in ``FileTarget2`` then ``nil`` is returned.

.. code-block:: mini

   FileTarget:exists

Returns ``FileTarget`` if ``FileTarget`` corresponds to an existing file in the file system, or if ``FileTarget`` has a build function.

.. code-block:: mini

   FileTarget:ls(Pattern, :R)

Returns a list of the files contained in ``FileTarget`` that match ``Pattern``. ``Pattern`` may be omitted in which case all files are returned. ``:R`` denotes a recursive listing, omit it to return only one level of files.

   ml_method_by_name("ls", 0, target_file_ls, FileTargetT, NULL);
   ml_method_by_name("copy", 0, target_file_copy, FileTargetT, FileTargetT, NULL);
   ml_method_by_name("open", 0, target_file_open, FileTargetT, MLStringT, NULL);
   ml_method_by_name("mkdir", 0, target_file_mkdir, FileTargetT, NULL);
   ml_method_by_name("rmdir", 0, target_file_rmdir, FileTargetT, NULL);
   ml_method_by_name("chdir", 0, target_file_chdir, FileTargetT, NULL);
   ml_method_by_name("path", 0, target_file_path, FileTargetT, NULL);