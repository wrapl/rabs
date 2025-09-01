
hierarchy
=========

.. graphviz::
   :layout: dot

   digraph hierarchy {
      rankdir="LR";
      fontsize="40pt"
      concentrate=false;
      overlap=true;
      splines=spline;
      packMode="node";
      mode="hier";
      outputorder="edgesfirst";
      mclimit=20;
      node [shape=box,fontsize=24];
      "targetset" [href="/library/targetset.html#targetset",target="_top",tooltip="sequence"];
      "sequence":e -> "targetset":w [color="0.840,1,0.5"];
      "symbol" [href="/library/symbol.html#symbol",target="_top",tooltip="target"];
      "target":e -> "symbol":w [color="0.394,1,0.5"];
      "scan" [href="/library/scan.html#scan",target="_top",tooltip="target"];
      "target":e -> "scan":w [color="0.783,1,0.5"];
      "meta" [href="/library/meta.html#meta",target="_top",tooltip="target"];
      "target":e -> "meta":w [color="0.798,1,0.5"];
      "file" [href="/library/file.html#file",target="_top",tooltip="target"];
      "target":e -> "file":w [color="0.912,1,0.5"];
      "expr" [href="/library/expr.html#expr",target="_top",tooltip="target"];
      "target":e -> "expr":w [color="0.198,1,0.5"];
      "target" [href="/library/target.html#target",target="_top",tooltip="any"];
      "any":e -> "target":w [color="0.335,1,0.5"];
      "context" [href="/library/context.html#context",target="_top",tooltip="any"];
      "any":e -> "context":w [color="0.768,1,0.5"];
   }

