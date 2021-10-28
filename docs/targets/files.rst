File Targets
============

File targets correspond to files or directories in the file system. Each file target has a path which is either absolute or relative to the root directory of the project.

To create a file target, call the built-in :mini:`file(Path)` function with the path to the file or use a method such as :mini:`/`, :mini:`%` or :mini:`dir`.

.. code-block:: mini

   var FileTarget := file("/path/filename.txt")
   var DirTarget := FileTarget:dir :> file("/path")
   var ObjectTarget := DirTarget / "test.o" :> file("/path/test.o")
   var SourceTarget := ObjectTarget % "c" :> file("/path/test.c")

When called with a relative path, the path of the current context is used as the base. See :doc:`/contexts` for more information.

.. note::

   If a file target is created with an absolute path that lies within the project directory, the path is converted to a relative path. This ensures that the file target still references the correct location even if the project directory is moved in the file system.

Methods applicable to file targets can be found :doc:`here </library/file>`. The methods applicable to general targets also apply, they can be found :doc:`here </library/target>`.

Path Resolution
---------------

Where possible, file targets are represented by paths relative to the project root. When required (e.g. use in a shell command), the full path of the file is computed. In the absence of virtual mounts, this simply the relative path appended to the root path. Virtual mounts allow for multiple directories to be overlayed and generating multiple candidate paths for each file target. In this case the first path found to have an existing file is selected. If the file does not exist at any candidate path, the first candidate path is selected. More details about virtual mounts can be found :doc:`here </vmounts>`.

Build Function
--------------

The build function for a file target should cause the file to be created. The result of the build function is ignored (excluding errors), but ``rabs`` will give a warning if the file does not exist after the build function has completed.

Update Detection
----------------

When checking for updated file targets, ``rabs`` first checks the file modification time. If the modification time has changed, then ``rabs`` computes the *SHA256* checksum of the file contents. If this has changed then the file is marked as updated.
