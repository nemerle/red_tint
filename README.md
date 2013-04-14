This repository is a port of mruby to c++
It's main aim is to make embedding ruby in c++ apps as painless as possible.

* The cmake build system has been restored and minirake one removed.
* MRuby parser.y has been trimmed down.
* Most of the parsing/compilation has been ported away from lisp-like structures to plain old AST classes.
* Memory management has been pulled out into it's own class.
* most of the c interface code was removed for the time being.

#### warning

This code base is using c++11 features that are not available in VS2010 and I have no way to check if it compiles under VS2012.
it was tested under gcc >= 4.7, and clang >= 3.2

The mruby original repository is [here](http://github.com/mruby/mruby)

---

## License

Copyright (c) 2012 mruby developers

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

## Note for License

mruby has chosen a MIT License due to its permissive license allowing
developers to target various environments such as embedded systems.
However, the license requires the display of the copyright notice and license
information in manuals for instance. Doing so for big projects can be
complicated or troublesome.
This is why mruby has decided to display "mruby developers" as the copyright name
to make it simple conventionally.
In the future, mruby might ask you to distribute your new code
(that you will commit,) under the MIT License as a member of
"mruby developers" but contributors will keep their copyright.
(We did not intend for contributors to transfer or waive their copyrights,
 Actual copyright holder name (contributors) will be listed in the AUTHORS file.)

Please ask us if you want to distribute your code under another license.

---

