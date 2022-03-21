
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
      "target":e -> "symbol":w;
      "sequence":e -> "targetset":w;
      "target":e -> "scan":w;
      "target":e -> "meta":w;
      "target":e -> "file":w;
      "target":e -> "expr":w;
      "any":e -> "target":w;
      "any":e -> "context":w;
   }

