TODO
----
* diffy-git does not handle file move well in column view
* Run algorithm in a separate thread. Add watchdog and an optional time limit.
   * Sometimes when used with git I'd just want it to skip a file if it would take too long to load.
      (it's typically large generated assets)
* Look into supporting arbitrary number of columns. Maybe with widths allocated up front?
* Make a default, dark and light theme, so we find a nice set of style attributes to provide
* I found a weird theme file on one computer where [color_map] had
  'red' = true. Try to reproduce.

Most probably happening at some point
-------------------------------------
* Improve diff performance where A and B have a lot of common lines at the beginning and the end of the file
* Limit width of diff window depending on line length

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