# Rabs

Rabs is an imperative build system, borrowing features from [http://omake.metaprl.org/index.html](Omake)
but implemented in C instead of OCaml and supporting an imperative paradigm (with functional components) instead of a pure functional one,.

## Introduction

## History

## Language

The language in Rabs is called *minilang* since it is not meant to be very sophisticated.
It is case sensitive with lower case keywords, ignores spaces and tabs but will treat an end-of-line
marker as the end of a statement unless additional code is expected (e.g. after an infix operator or
in a function call).

### Sample Code

```lua
c_includes := fun(Source) do
	var Files := []
	var Lines := shell('gcc {CFLAGS} -I{Source:dir} -M -MG {Source}')
	var Start, File := ""
	var I := for J := 1 .. Lines:length do
		if Lines[J, J + 2] = ": " then
			exit J + 2
		end
	end
	loop while I <= Lines:length
		var Char := Lines[I]
		if Char <= " " then
			if File != "" then
				Files:put(file(File))
				File := ""
			end
		elseif Char = "\\" then
			I := old + 1
			Char := Lines[I]
			if Char = " " then
				File := '{old} '
			end
		else
			File := '{old}{Char}'
		end
		I := old + 1
	end
	return Files
end
```

## Usage

### Contexts

Rabs executes minilang code within _contexts_. Each context maintains its own set of variables, and is associated with a directory in the file system.
This directory is used to resolve relative file paths evaluated within the context.

Contexts are hierarchical, each context has exactly one parent context (typically associated with the parent directory), and variables undefined in one context and searched for up the parent context chain.
However, assigning a variable in a context only changes its value within that context and its children. 
This means the variables defined in a context are automatically available to child contexts and that parent values of variables can be extended or modified within a context and its children without affected its parents. 

There is exactly one **root** context, associated with the root directory of the project / build.

New contexts are made when entering a child directory using [`subdir()`](#subdir) or by calling [`scope()`](#scope).

### Project Layout

Rabs loads and executes code from build files named `_minibuild_`.
When run, rabs first searches for the root `_minibuild_` file by ascending directories until it finds a `_minibuild_` file starting with `-- ROOT --`.

`_minibuild_` files can be located in nested folders and loaded using [`subdir()`](#subdir).

```
<Project root>
├── _minibuid_
├── <Sub folder>
|   ├── _minibuild_
|   ├── <Sub folder>
|   |   ├── _minibuild_
|   |   ├── <Files>
|   |   └── ...
|   └── ...
⋮
├── <Sub folder>
|   ├── _minibuild_
|   └── ...
└─── <Files>
```



### Built-in Functions

* [`context()`](#context)
* [`scope(Name, Callback)`](#scope)
* [`subdir(TargetDir)`](#subdir)
* [`vmount(TargetDir, SourceDir)`](#vmount)
* [`file(FileName)`](#file)
* [`meta(Name)`](#meta)
* [`expr(Name)`](#expr)
* [`include(File)`](#include)
* [`execute(Command ...)`](#execute)
* [`shell(Command ...)`](#shell)
* ~[`mkdir(File)`](#mkdir)~
* ~[`open(String)`](#open)~
* [`print(Values ...)`](#print)
* [`getenv(Key)`](#getenv)
* [`setenv(Key, Value)`](#setenv)
* [`defined(Key)`](#defined)

#### `context()`

Returns the name of the current context as a string.

#### `scope(Name, Callback)`

#### `subdir(TargetDir)`

Enters _`TargetDir`_ and loads the file `_minibuild_` within a new context.

#### `vmount(TargetDir, SourceDir)`

Virtually mounts / overlays `SourceDir` over `TargetDir` during the build.
This means that when a [file](#file-and-directory-targets) whose path contains `TargetDir` is referenced, the build system will look an existing file in two locations and return whichever exists, or return the unchanged path if neither exists.

More specifically, in order to convert a file object with path _`TargetDir`/path/filename_ to a full file path, the build system will

1.	return _`TargetDir`/path/filename_ if a file exists at this path
2.	return _`SourceDir`/path/filename_ if a file exists at this path
3.	return _`TargetDir`/path/filename_

Multiple virtual mounts can be nested, and the build system will try each possible location for an existing file and return that path, returning the unchanged path if the file does not exist in any location.

The typical use for this function is to overlay a source directory over the corresponding build output directory, so that build commands can be run in the output directory but reference source files as if they were in the same directory.

#### `file(FileName)`

Creates a [file target](#file-and-directory-targets).

#### `meta(Name)`

Creates a [meta target](#meta-targets).

#### `expr(Name)`

Creates an [expression target](#expression-targets).

#### `include(File)`

#### `execute(Command, ...)`

#### `shell(Command, ...)`

#### ~`mkdir(File | String)`~

#### ~`open(File | String, String)`~

### The Build Process

Everything in the Rabs build tree is considered a _target_.
Every target has a unique id, and every unique id corresponds to a unique target.
This means that when constructing a target anywhere in the build tree, if the construction results in the same id, then it will return the same target.

Every target has a (possibly empty) set of dependencies, i.e. targets that must be built before this target is built.
Cyclic dependencies are not allowed, and will trigger an error.

Each run of Rabs is considered an iteration, and increments an internal iteration counter in the build database.
In order to reduce unneccesary building for large project, at each iteration Rabs decides both whether a target needs to be built and whether, after building, it has actually changed.

If a target is missing (e.g. for the first build, when a new target is added to the build or for a file that is missing from the file system), then it needs to be built.
Once built, the build database records two iteration values for each target:

1. The last built iteration: when the target was last built.
2. The last changed iteration: when the target was last changed.

Since a target can't change without being built, the last built iteration of a target is always greater or equal to the last changed iteration.
The last built iteration of a target should be greater or equal to the last changed iteration of its dependencies.

While building, if a target has a last built iteration less than the last changed iteration of any of its dependencies, then it is rebuilt, and its last built iteration updated to the current iteration.
Then it is checked for changes (using hashing, depending on the target type), and the last changed iteration updated if it has indeed changed.
This will trigger other target to be rebuilt as required. 

#### Targets

##### Methods

* `Target[Dependencies...]`: adds dependcies to `Target`. `Dependencies` can be individual dependencies or lists of dependencies which are expanded recursively. Returns `Target`.
* `Target => BuildFunction`: sets the build function for `Target`.
* `Target:scan(Name)`: creates a [scan target](#scan-targets) for `Target`.

#### File and Directory Targets

##### Methods

* `File:exists`: returns `:true` if `File` exists, otherwise `nil`.
* `File:open(Mode)`: opens `File` in read, write or append mode depending on `Mode` (`r`, `w` or `a`) and returns a [file handle](#file-handles).
* `File:dir`: returns the directory containing `File`.
* `File:basename`: returns the name of `File` without its path or extension.
* `File % Extension`: returns a new file target by replacing the extension of `File` with `Extension`.
* `File:copy(Dest)`: copies the contents of `File` to the file target at `Dest`.
* `Dir:mkdir`: creates a directory (and all missing parent directories).
* `Dir:ls([Filter])`: returns an iterator of files in `Dir`, filtered by an optional regular expression.
* `Dir / FileName`: returns a new file target for the file called `FileName` in `Dir`.

#### Scan Targets

#### Meta Targets

#### Expression Targets

#### Symbol Targets

### Other Builtin Features

#### File Handles

##### Methods

* `Handle:read(...)`
* `Handle:write(...)`
* `Handle:close`
