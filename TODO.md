TODO
----
-

Most probably happening at some point
-------------------------------------
* Improve diff performance where A and B have a lot of common lines at the beginning and the end of the file
* Allow arbitrary color names in config

Most likely not happening
-------------------------
* Annotate_tokens can be threaded to compute the token diff used for the column view
* Feed the annotated diff hunks to the output renderer on-the-go instead of precomputing upfront
* Config does not handle comment serialization correctly (some are lost)
* Fix rendering issues in Windows cmd (implement DisplayCommand renderer using the win32 api? ugh)
* Compare patience algorithm with `git diff --patience`
* Profiling and optimization

Not happening
-------------
* Implement parser for unified diffs so that we can outsource performance issues & implementation
  details to better tools.