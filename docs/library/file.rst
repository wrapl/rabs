.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

file
====

.. _type-filetarget:

:mini:`type filetarget < target`
   A file target represents a single file or directory in the filesystem.
   File targets are stored relative to the project root whenever possible,  taking into account virtual mounts. They are automatically resolving to absolute paths when required.


:mini:`meth (File: filetarget) % (Extension: string): filetarget`
   Returns a new file target by replacing the extension of :mini:`File` with :mini:`Extension`.


:mini:`meth (Target: filetarget) - (Source: filetarget): string | nil`
   Returns the relative path of :mini:`Target` in :mini:`Source`,  or :mini:`nil` if :mini:`Target` is not contained in :mini:`Source`.


:mini:`meth (Directory: filetarget) / (Name: string): filetarget`
   Returns a new file target at :mini:`Directory`\ ``/``\ :mini:`Name`.


:mini:`meth (Target: filetarget):basename: string`
   Returns the filename component of :mini:`Target`.


:mini:`meth (Directory: filetarget):chdir: filetarget`
   Changes the current directory to :mini:`Directory`. Returns :mini:`Directory`.


:mini:`meth (Source: filetarget):copy(Dest: filetarget): nil`
   Copies the contents of :mini:`Source` to :mini:`Dest`.


:mini:`meth (Target: filetarget):dir: filetarget`
   Returns the directory containing :mini:`Target`.


:mini:`meth (Target: filetarget):dirname: string`
   Returns the directory containing :mini:`Target` as a string. Virtual mounts are not applied to the result (unlike :mini:`Target:dir`).


:mini:`meth (Target: filetarget):exists: filetarget | nil`
   Returns :mini:`Target` if the file or directory exists or has a build function defined,  otherwise returns :mini:`nil`.


:mini:`meth (Target: filetarget):extension: string`
   Returns the file extension of :mini:`Target`.


:mini:`meth (Directory: filetarget):ls(Pattern?: string|regex, Recursive?: method, Filter?: function, ...): list[target]`
   Returns a list of the contents of :mini:`Directory`. Passing :mini:`:R` results in a recursive list.


:mini:`meth (Target: filetarget):map(Source: filetarget, Dest: filetarget): filetarget | error`
   Returns the relative path of :mini:`Target` in :mini:`Source` applied to :mini:`Dest`,  or an error if :mini:`Target` is not contained in :mini:`Source`.


:mini:`meth (Directory: filetarget):mkdir: filetarget`
   Creates the all directories in the path of :mini:`Directory`. Returns :mini:`Directory`.


:mini:`meth (Target: filetarget):open(Mode: string): file`
   Opens the file at :mini:`Target` with mode :mini:`Mode`.


:mini:`meth (Target: filetarget):path: string`
   Returns the internal (possibly unresolved and relative to project root) path of :mini:`Target`.


:mini:`meth (Target: filetarget):rmdir: filetarget`
   Removes :mini:`Target` recursively. Returns :mini:`Directory`.


.. _fun-file:

:mini:`fun file(Path: string): filetarget`
   Returns a new file target. If :mini:`Path` does not begin with `/`,  it is considered relative to the current context path. If :mini:`Path` is specified as an absolute path but lies inside the project directory,  it is converted into a relative path.


