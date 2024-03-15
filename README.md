# scb

A simple frontend for grepping around source code. 

## Installation

```
shell> make
```

Then install `ssrc` and `scb` someplace that's in your `$PATH`. For
example,

```
shell> install ssrc /usr/local/bin
shell> install scb /usr/local/bin
```

## How to use scb

To search the keyword `main` in a codebase that's in a folder called
`foobarcode`, do

```
shell> ssrc main /path/to/foobarcode | scb
```

This will bring up the scb viewer. In the viewer you can browse the
results of the search and open any result in your default editor set
by `$EDITOR`. If `$EDITOR` isn't set, `scb` will default to `vim`.

You don't have to use `ssrc`. It's just a wrapper around `grep`. You
can pipe in anything that conforms to format `filename:line
number:content` for each line. For example,

```
shell> grep -H -n '^#' README.md | scb
```

will give you a list of all the headers in this file.

### Key Bindings

| Key         | Behavior                   |
|-------------|----------------------------|
| q           | Quit                       |
| *Up*        | Select line above.         |
| *Down*      | Select line below.         |
| *Page Up*   | Scroll up half a screen.   |
| *Page Down* | Scroll down half a screen. |
| *Enter*     | Open file in `$EDITOR`.    |
| /           | Search for keywords.       |
| :           | Go to line.                |
  
### Mouse Support

You may scroll with your mouse wheel if it's supported on your
system. Right click will open the selected line.
