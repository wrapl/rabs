.. include:: <isonum.txt>

.. include:: <isoamsa.txt>

.. include:: <isotech.txt>

general
=======

.. rst-class:: mini-api

:mini:`fun chdir(Path..: any): nil`
   Changes the current directory to :mini:`Path`.


:mini:`fun execute(Command..: any): nil | error`
   Builds a shell command from :mini:`Command..` and executes it,  discarding the output. Returns :mini:`nil` on success or raises an error.


:mini:`fun mkdir(Path..: any): nil | error`
   Creates a directory with path :mini:`Path` if it does not exist,  creating intermediate directories if required.


:mini:`fun open(Path: any, Mode: string): file`
   Opens the file at path :mini:`Path` with the specified mode.


:mini:`fun print(Values..: any): nil`
   Prints out :mini:`Values` to standard output.


:mini:`fun shell(Command..: any): string | error`
   Builds a shell command from :mini:`Command..` and executes it,  capturing the output. Returns the captured output on success or raises an error.


:mini:`fun execv(Command: list): nil | error`
   Similar to :mini:`execute()` but expects a list of individual arguments instead of letting the shell split the command line.


:mini:`fun shellv(Command: list): nil | error`
   Similar to :mini:`shell()` but expects a list of individual arguments instead of letting the shell split the command line.


:mini:`fun defined(Name: string): string | nil`
   If :mini:`Name` was defined in the *rabs* command line then returns the associated value,  otherwise returns :mini:`nil`.


:mini:`fun getenv(Name: string): string | nil`
   Returns the current value of the environment variable :mini:`Name` or :mini:`nil` if it is not defined.


:mini:`fun include(Path..: string): any`
   Loads the Minilang file or shared library specified by :mini:`Path`.


:mini:`fun scope(Name: string, Function: function): any`
   Creates a new scoped subcontext with name :mini:`Name` and calls :mini:`Function()` in the new context.


:mini:`fun setenv(Name: string, Value: string): nil`
   Sets the value of the environment variable :mini:`Name` to :mini:`Value`.


:mini:`fun subdir(Name: string): context | error`
   Creates a new directory subcontext with name :mini:`Name` and loads the :file:`build.rabs` file inside the directory.
   Returns an error if the directory does not exist or does not contain a :file:`build.rabs` file.


:mini:`fun vmount(Path: string, Source: string): nil`
   Mounts the directory :mini:`Source` onto :mini:`Path`. Resolving a file in :mini:`Path` will also check :mini:`Source`.


:mini:`fun target(Path: string): target | error`
   Returns the target with path :mini:`Path` if is has been defined,  otherwise raises an error.


:mini:`fun check(Target..: target): nil`
   Checks that each :mini:`Target` is up to date,  building if necessary.


