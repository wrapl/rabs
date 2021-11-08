Virtual Mounts
==============

Virtual mounts allow directories to be overlayed, which in turn allows source and build directories to be kept separate without adding a lot of path overhead to the build functions.

Virtual mounts are created using the :mini:`vmount(Path, Source)` function. Both :mini:`Path` and :mini:`Source` should be directory paths. :mini:`Path` must be a relative path to the current context path, :mini:`Source` can be either relative or absolute.

Once created, virtual mounts are visible project wide.

.. code-block:: mini

   vmount("build", "source")

Path Resolution
---------------

Whenever a file target is used, it is first resolved to an absolute path. Each virtual mount :mini:`vmount(Path, Source)` adds creates two possible resolutions for the file path, the original and one with :mini:`Path` replaced with :mini:`Source`. Virtual mounts are considered in reverse order, later virtual mounts are applied first.

Resolving a file target to a path involves checking each possible resolution for an existing file or directory. If found, the path of the existing file or directory is returned. Otherwise the first possible resolution, i.e. without any vmount replacements, is returned.

.. code-block:: mini

   vmount("a", "b")
   vmount("a/c", "d")
   
   file("a/c/test") :> Tries "a/c/test", "d/test", "b/c/test" in that order

.. note::

   When using :mini:`subdir` with a virtual mount, the target path should be used, not the source, even though the :file:`build.rabs` file in likely in the source directory. ``rabs`` will treat the target path as part of the project and only use the source directory for finding files, including the :file:`build.rabs` file.

