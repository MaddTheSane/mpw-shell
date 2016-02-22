MPW Shell
---------

MPW Shell is a re-implementation of the Macintosh Programmer's Workshop shell.
The primary reason is to support MPW Make (which generated shell script). It
may also be useful for other things.

Supported features
------------------
* If ... [Else If] ... [Else] ... End
* Begin ... End
* ( ... )
* ||
* &&
* redirection

Not supported
-------------
* pipes (|)
* subshells (`...`, ``...``)
* text-editing commands (search forward/backward, regular expressions, et cetera)

Builtin Commands
----------------
* Directory
* Echo
* Export
* Parameters
* Quote
* Set
* Unexport
* Unset
* Which


Setup
-----
1. Install MPW.  The mpw binary should be somewhere in your `$PATH`.
It also checks `/usr/local/bin/mpw` and `$HOME/mpw/bin/mpw`
2. Copy the `Startup` script to `$HOME/mpw/`.  This script is executed
when mpw-shell (or mpw-make) starts up (imagine that) and should
be used to set environment variables.
