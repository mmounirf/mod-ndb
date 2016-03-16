# Older News #


## Changes from mod\_ndb 1.0 rc to mod\_ndb 1.0 release ##
Release 1.0 includes the following changes since 1.0-rc:
  * ([r465](https://code.google.com/p/mod-ndb/source/detail?r=465)) The t.sql utility in the test suite would hang ( "test.list" was not supplied).
  * ([r480](https://code.google.com/p/mod-ndb/source/detail?r=480)) Fix error exit in "configure" to indicate which option was bad.
  * ([r485](https://code.google.com/p/mod-ndb/source/detail?r=485)) Fix to runner.awk in the test suite -- awk substrings start at 1, not 0.
  * ([r491](https://code.google.com/p/mod-ndb/source/detail?r=491)) Fix undefined symbol with Apache 1.3 ([issue #56](https://code.google.com/p/mod-ndb/issues/detail?id=#56))


## NEWS: mod\_ndb gets a SQL parser ##
So far, mod\_ndb has been limited by what can be specified in one-line Apache configuration directives.  But now it supports "N-SQL," which looks a lot like SQL.  It is not any more powerful than the old-style configuration -- it doesn't have an optimizer, it can't do joins, and it requires you to tell it which indexes to use -- but probably does allow for more succinct and intuitive configuration of HTTP endpoints.    


## Notes on the 'rev 320' release ##
  * This release comes a few days before the 2007 MySQL Conference, and it's the basis for my talk there.
  * It includes user-defined output formats.
  * There's a new 'Quick Reference Card' PDF file in the distribution.

## New mailing list ##
I created a general discussion list -- you can join it by following the 'Groups' link on this page.

## Notes on the 'rev 284' release ##
  * This release adds support for many MySQL data types, including decimal, tinyint, smallint, mediumint, date, time, and datetime
  * Internally, the architecture of mod\_ndb's output formats has changed, as a first step towards allowing user-defined  formats.

## Notes on the 'rev 254' release ##
  * See the [mod\_ndb wiki](http://forge.mysql.com/wiki/ProjectPage_mod_ndb) over at MySQL Forge for documentation.
  * This one compiles properly with MySQL 5.1.16.
  * It introduces rudimentary XML output, plus correct encoding of escaped characters (quote and backslash) in both XML and JSON.
  * It introduces support for table scans.
  * After you build the module, 'vi test.conf' + 'make start' should succesfully start the server now on many more platforms than before.