TODO
----
* Theme selection
  * (Color scheme support on top of fg/bg/attr)
* Move config/parser to util/config_parser
* Improve color rendering to support 16 bit palettes and 24 bit color codes
* Investigate failing test cases (looks ok when checking manually; error in test runner?)

Most likely not happening
-------------------------
* Config does not handle comment serialization correctly (some are lost)
* Fix rendering issues in Windows cmd (implement DisplayCommand renderer using the win32 api? ugh)
* Compare patience algorithm with `git diff --patience`
* Profiling and optimization

Not happening
-------------
* Implement parser for unified diffs so that we can outsource performance issues & implementation
  details to better tools.