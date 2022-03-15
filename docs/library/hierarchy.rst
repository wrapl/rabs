
hierarchy
=========

.. graphviz::

   digraph hierarchy {
      size="180,120";
      rankdir="LR";
      fontsize="40pt"
      concentrate=true;
      overlap=false;
      packMode="node";
      outputorder="edgesfirst";
      node [shape=box,fontsize=24];
      "target":e -> "symbtarget":w;
      "sequence":e -> "targetset":w;
      "target":e -> "scantarget":w;
      "target":e -> "metatarget":w;
      "target":e -> "filetarget":w;
      "target":e -> "exprtarget":w;
      "any":e -> "target":w;
      "any":e -> "context":w;
   }

