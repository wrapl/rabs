
hierarchy
=========

.. graphviz::

   digraph hierarchy {
      rankdir="LR";
      fontsize="40pt"
      concentrate=true;
      overlap=false;
      packMode="node";
      outputorder="edgesfirst";
      node [shape=box,fontsize=24];
      "sequence":e -> "targetset":w;
      "target":e -> "symbol":w;
      "target":e -> "scan":w;
      "target":e -> "meta":w;
      "target":e -> "file":w;
      "target":e -> "expr":w;
      "any":e -> "target":w;
      "any":e -> "context":w;
   }

