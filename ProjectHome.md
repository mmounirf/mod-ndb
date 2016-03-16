Mod\_ndb is an Apache module that provides a Web Services API for MySQL Cluster.  It creates a direct connection from an Apache Web server to the NDB data nodes in a MySQL Cluster (bypassing mysqld, and eliminating the need to create or parse SQL queries at run-time).  This allows you to query and modify a database over HTTP using GET, POST, and DELETE requests, and fetch results in a variety of formats, including JSON results that can go 'direct to the browser' in an Ajax application.  Mod\_ndb is also scriptable, using Apache's subrequest API, so that complex multi-table transactions or joins (even including sort-merge join plans that are currently not possible in MySQL) can be easily created in PHP or Perl.

# What's new in mod\_ndb 1.1 #
  * **Easier to build:**  Now you can build mod\_ndb for Apache 2 without requiring apxs from apache 1.3.
  * Support for multiple TEXT or BLOB columns in a result
  * Support for the server side of the [JSONRequest proposal](http://json.org/JSONRequest.html)
  * A new multithreaded performance test using httperf.

## Changes since mod\_ndb 1.1 b496 ##
  * Fixes for [issue#58](https://code.google.com/p/mod-ndb/issues/detail?id=#58) , [issue#67](https://code.google.com/p/mod-ndb/issues/detail?id=#67) , [issue#73](https://code.google.com/p/mod-ndb/issues/detail?id=#73)
  * The configure script automatically finds apr-config and apu-config
  * An important fix for a linker issue ([r523](https://code.google.com/p/mod-ndb/source/detail?r=523))
  * New "JSON + XML" encoding for output formats


## Links ##

  * If you check out mod\_ndb from subversion, you will need the [Coco/R compiler generator for C++](http://www.ssw.uni-linz.ac.at/Research/Projects/Coco/#CPP) to build the parsers.   (Be warned to unpack the CocoSourcesCPP.zip file into an empty directory).
  * Documentation is hosted on the wiki at [MySQL Forge](http://forge.mysql.com/wiki/ProjectPage_mod_ndb)
  * The mod\_ndb mailing list is hosted on [Google Groups](http://groups.google.com/group/mod-ndb)
  * OlderNews