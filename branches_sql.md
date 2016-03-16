# The "sql" branch in svn #

The _sql_ branch is a feature branch, for integrating Tulay's new NSQL parser into the code.

In this branch I have also reorganized the source tree.  There are four second-level directories ( Core, Format, JSON, and NSQL ) which each has a module.mk file.  When the configure script creates the top-level Makefile, it will copy each module.mk into the Makefile.  (It would also be possible to use the "include" directive that is supported by most versions of _make_, but for now that's not how I've done it).

The old parser is also still included, under N-SQL.  You should be able to build mod\_ndb with the old parser by giving configure the `--old-nsql` flag.

Also in this branch a new "make tools" rule can be used to build stand-alone test programs.