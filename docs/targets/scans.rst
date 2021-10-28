Scan Targets
============

A scan target represents a computable list of other targets that is updated as required.

To create a scan target, call the :mini:`:scan(Name)` method on another *base* target. Calling :mini:`:scan(Name)` with a name of an existing scan target for the same base target will simply return the existing target.

.. code-block:: mini

   var FileTarget := file("test.o")
   var ScanTarget := FileTarget:scan("HEADERS")

Build Function
--------------

The build function for a scan target should return a list of targets. Each of the targets returned is then updated if necessary.

.. note::

   The targets returned by a scan target's build function are not automatically considered dependencies of the scan target itself. In some cases (e.g. C include files), this behaviour is required in which case the dependencies need to be added explicitly. Since the list of targets is not known until the scan target's build function is run, the only way to do this is to use the :mini:`check(Targets)` which will add :mini:`Targets` as dependencies to the current building target.

Update Detection
----------------

A scan target is considered updated when any of its list of targets has been updated.
