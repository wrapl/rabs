scan
====

:mini:`type scantarget < target`
   A scan target is a dynamic set of targets derived from another base target.

   The build function for a scan target must return a list of targets.


:mini:`meth (Target: scantarget):source: target`
   Returns the base target for the scan.


:mini:`meth (Target: scantarget):scans: targetset`
   Returns the results of the last scan.


