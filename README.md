# Rabs

Rabs is an imperative build system, borrowing features from [http://omake.metaprl.org/index.html](Omake)
but implemented in C instead of OCaml and supporting an imperative paradigm instead of a functional one.

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

### Built-in Functions

* [`vmount(TargetDir, SourceDir)`](#vmount)
* [`subdir(TargetDir)`](#subdir)
* [`file(FileName)`](#file)
* [`meta(Name)`](#meta)
* [`expr(Name)`](#expr)
* [`include()`](#include)
* [`context()`](#context)
* [`execute(Command ...)`](#execute)
* [`shell(Command ...)`](#shell)
* [`mkdir(File)`](#mkdir)
* [`scope(Name, Callback)`](#scope)
* [`print(Values ...)`](#print)
* [`open(File)`](#open)
* [`getenv(Key)`](#getenv)
* [`setenv(Key, Value)`](#setenv)
* [`defined(Key)`](#defined)

#### `vmount(TargetDir, SourceDir)`

Virtually mounts / overlays `SourceDir` over `TargetDir` during the build. This means that when a [`File` object](#file) whose path contains `TargetDir` is referenced, the build system will look an existing file in two locations and return whichever exists, or return the unchanged path if neither exists.

More specifically, in order to convert a file object with path _`TargetDir`/path/filename_ to a full file path, the build system will

1.	return _`TargetDir`/path/filename_ if a file exists at this path
2.	return _`SourceDir`/path/filename_ if a file exists at this path
3.	return _`TargetDir`/path/filename_

Multiple virtual mounts can be nested, and the build system will try each possible location for an existing file and return that path, returning the unchanged path if the file does not exist in any location.

The typical use for this function is to overlay a source directory over the corresponding build output directory, so that build commands can be run in the output directory but reference source files as if they were in the same directory.

#### `subdir(TargetDir)`

### Project Layout

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

