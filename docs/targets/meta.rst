Meta Targets
============

A meta target represents a target with no other properties other than a build function and dependencies on other targets.

To create a meta target, call the built-in :mini:`meta(Name)` function with a name that is unique to the current context. Calling :mini:`meta(Name)` with a name that already of an existing meta target in the current context will simply return the existing target.

.. code-block:: mini

   var MetaTarget := meta("TESTS")

Each directory :doc:`context </contexts>` defines a :mini:`DEFAULT` meta target. If a directory context is not the root context then its :mini:`DEFAULT` target is automatically added as a dependency of the parent context's :mini:`DEFAULT` target.

Build Function
--------------

The build function for a meta target can do anything. The result of the build function is ignored (excluding errors).

Update Detection
----------------

A meta target is considered updated if any of its dependencies has been updated.
