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

Virtually mounts / overlays `SourceDir` over `TargetDir` during the build. This means that when a [`File` object](#file) whose path contains `TargetDir` is referenced, the build system will look an existing file in two locations and return whichever exists, or return the unchanged path if neither exists.

More specifically, in order to convert a file object with path _`TargetDir`/path/filename_ to a full file path, the build system will

1.	return _`TargetDir`/path/filename_ if a file exists at this path
2.	return _`SourceDir`/path/filename_ if a file exists at this path
3.	return _`TargetDir`/path/filename_

Multiple virtual mounts can be nested, and the build system will try each possible location for an existing file and return that path, returning the unchanged path if the file does not exist in any location.

The typical use for this function is to overlay a source directory over the corresponding build output directory, so that build commands can be run in the output directory but reference source files as if they were in the same directory.

#### `file(FileName)`

Creates a [File Target](#files-and-directories).

#### `meta(Name)`

#### `expr(Name)`

#### `include(File)`

#### `execute(Command, ...)`

#### `shell(Command, ...)`

#### ~`mkdir(File | String)`~

#### ~`open(File | String, String)`~

### Target Types

#### Files and Directories

##### Methods

* `File:exists`: returns `:true` if `File` exists, otherwise `nil`.
* `File:open(Mode)`: opens `File` in read, write or append mode depending on `Mode` (`r`, `w` or `a`).
* `File:dir`: returns the directory containing `File`.
* `File:basename`: returns the name of `File` without its path or extension.
* `File % Extension`: returns a new file target by replacing the extension of `File` with `Extension`.
* `File:copy(Dest)`: copies the contents of `File` to the file target at `Dest`.
* `File:scan(Name)`: creates a [Scan Target](#scan_target) for `File`.
* `Dir:mkdir`: creates a directory (and all missing parent directories).
* `Dir:ls([Filter])`: returns an iterator of files in `Dir`, filtered by an optional regular expression.
* `Dir / FileName`: returns a new file target for the file called `FileName` in `Dir`.

