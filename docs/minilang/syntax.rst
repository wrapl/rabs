Syntax
======

*Minilang* has a simple syntax, not too different from *Pascal* (although probably closer to *Oberon-2*). Keywords are in lower case, statements are delimited by semicolons ``;``, although these can be (and are usually) omitted at the end of a line. The following is an example of *Minilang* code showing an implementation of the Fibonacci numbers.

.. code-block:: mini

   fun fibonacci(N) do
      if N <= 0 then
         error("RangeError", "N must be postive")
      elseif N <= 2 then
         ret 1
      end
      var A := 1, B := 1
      for I in 2 .. (N - 1) do
         var C := A + B
         A := B
         B := C
      end
      ret B
   end
   
   for I in 1 .. 10 do
      print('fibonacci({I}) = {fibonacci(I)}\n')
   end
   
   print('fibonacci({0}) = {fibonacci(0)}\n')

This produces the following output:

::

   $ ./minilang test/test12.mini
   fibonacci(1) = 1
   fibonacci(2) = 1
   fibonacci(3) = 2
   fibonacci(4) = 3
   fibonacci(5) = 5
   fibonacci(6) = 8
   fibonacci(7) = 13
   fibonacci(8) = 21
   fibonacci(9) = 34
   fibonacci(10) = 55
   Error: N must be postive
      test/test12.mini:3
      test/test12.mini:20




      