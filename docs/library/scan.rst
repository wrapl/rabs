.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

scan
====

.. _type-scan:

:mini:`type scan < target`
   A scan target is a dynamic set of targets derived from another base target.
   The build function for a scan target must return a list of targets.


.. _fun-scan:

:mini:`fun scan(Target: target, Name: string, BuildFn?: any): scan`
   Returns a new scan target using :mini:`Target` as the base target.


:mini:`meth (Target: scan):scans: targetset`
   Returns the results of the last scan.


:mini:`meth (Target: scan):source: target`
   Returns the base target for the scan.


