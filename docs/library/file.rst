.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

file
====

.. rst-class:: mini-api

.. _type-file:

:mini:`type file < target`
   A file target represents a single file or directory in the filesystem.
   File targets are stored relative to the project root whenever possible,  taking into account virtual mounts. They are automatically resolving to absolute paths when required.


.. _fun-file:

:mini:`fun file(Path: string, BuildFn?: any): file`
   Returns a new file target. If :mini:`Path` does not begin with `/`,  it is considered relative to the current context path. If :mini:`Path` is specified as an absolute path but lies inside the project directory,  it is converted into a relative path.


:mini:`meth (File: file) % (Extension: string): file`
   Returns a new file target by replacing the extension of :mini:`File` with :mini:`Extension`.


:mini:`meth (Target: file) - (Source: file): string | nil`
   Returns the relative path of :mini:`Target` in :mini:`Source`,  or :mini:`nil` if :mini:`Target` is not contained in :mini:`Source`.


:mini:`meth (Directory: file) / (Name: string): file`
   Returns a new file target at :mini:`Directory`\ ``/``\ :mini:`Name`.


:mini:`meth (Target: file):basename: string`
   Returns the filename component of :mini:`Target`.


:mini:`meth (Directory: file):chdir: file`
   Changes the current directory to :mini:`Directory`. Returns :mini:`Directory`.


:mini:`meth (Source: file):copy(Dest: file): nil`
   Copies the contents of :mini:`Source` to :mini:`Dest`.


:mini:`meth (Target: file):dir: file`
   Returns the directory containing :mini:`Target`.


:mini:`meth (Target: file):dirname: string`
   Returns the directory containing :mini:`Target` as a string. Virtual mounts are not applied to the result (unlike :mini:`Target:dir`).


:mini:`meth (Target: file):exists: file | nil`
   Returns :mini:`Target` if the file or directory exists or has a build function defined,  otherwise returns :mini:`nil`.


:mini:`meth (Target: file):extension: string`
   Returns the file extension of :mini:`Target`.


:mini:`meth (Directory: file):ls(Pattern?: string|regex, Recursive?: method, Filter?: function, ...): list[target]`
   Returns a list of the contents of :mini:`Directory`. Passing :mini:`:R` results in a recursive list.


:mini:`meth (Target: file):map(Source: file, Dest: file): file | error`
   Returns the relative path of :mini:`Target` in :mini:`Source` applied to :mini:`Dest`,  or an error if :mini:`Target` is not contained in :mini:`Source`.


:mini:`meth (Directory: file):mkdir: file`
   Creates the all directories in the path of :mini:`Directory`. Returns :mini:`Directory`.


:mini:`meth (Target: file):open(Mode: string): stream`
   Opens the file at :mini:`Target` with mode :mini:`Mode`.


:mini:`meth (Target: file):path: string`
   Returns the internal (possibly unresolved and relative to project root) path of :mini:`Target`.


:mini:`meth (Target: file):rmdir: file`
   Removes :mini:`Target` recursively. Returns :mini:`Directory`.


