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

* `vmount(TargetDir, SourceDir)`
* `subdir(TargetDir)`
* `file(FileName)`
* `meta(Name)`
* `expr(Name)`
* `include()`
* `context()`
* `execute(Command ...)`
* `shell(Command ...)`
* `mkdir(File)`
* `scope(Name, Callback)`
* `print(Values ...)`
* `open(File)`
* `getenv(Key)`
* `setenv(Key, Value)`

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

