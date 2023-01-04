TODO
----
* General cleanup
* Theme selection
* Improve color rendering to support 16 bit palettes and 24 bit color codes
* Investigate failing test cases (looks ok when checking manually; error in test runner?)

Most likely not happening
-------------------------
* Fix rendering issues in Windows cmd (implement DisplayCommand renderer using the win32 api? ugh)
* Compare patience algorithm with `git diff --patience`
* Profiling and optimization

Not happening
-------------
* Implement parser for unified diffs so that we can outsource performance issues & implementation
  details to better tools.