# Architectural Roadmap #

Here are the most important paths for mod\_ndb development, starting from the 1.1 release.

## Better SQL WHERE clauses ##
The "N-SQL" language in mod\_ndb  1.0 and 1.1 requires the application developer to know about the indexed structure of the database.  To write a query, you have to know what column is the primary key, or what the other indexes are.  While this is useful for performance, it misses one of the core ideas of the SQL language: that the developer states what he wants, and the SQL implementation decides how to to get it.  In SQL you should at least be able to write a WHERE clause based on column names, not access paths.

This includes [issue#46](https://code.google.com/p/mod-ndb/issues/detail?id=#46) and [issue#68](https://code.google.com/p/mod-ndb/issues/detail?id=#68).

## Pluggable SQL Functions ##
Another important part of SQL is a function library.  For two examples, we want to be able to say:
  * SELECT length(doc), doc from doc\_table where id = $x;
  * SELECT id from sys\_changes WHERE date = today();
These functions should not be built-in to mod\_ndb, but loadable as standard Apache modules.   All of the "loading" infrastructure should be provided by apache, with mod\_ndb simply providing a way for each module to register the functions that it provides.

[Issue #31](https://code.google.com/p/mod-ndb/issues/detail?id=#31) was an early description of this.  [Issue #60](https://code.google.com/p/mod-ndb/issues/detail?id=#60) begins to describe the module API.

## Cursor API ##
Some languages -- PHP, Perl, etc. -- can run inside the apache web server.   There is already a "cheap" API between mod\_ndb and these languages: they can call in to mod\_ndb using an Apache subrequest, and fetch a result from an Apache note. But this API is lacking in some ways: it is not familiar to application developers, or efficient, or powerful.

mod\_ndb should export a cursor API to these languages, as described (somewhat) in [issue #59](https://code.google.com/p/mod-ndb/issues/detail?id=#59).  The API itself should be something like a subset of ODBC or SQL-99/CLI.  You should be able to Prepare() any mod\_ndb SQL query; Bind() parameters to it; Execute() it with the parameters, and then Fetch() the result.  Prepare() should always run at startup time. It should be exported as a C API, so that you can build a familiar one-file Apache module in C, write an application using the SQL/CLI - like code, and have Apache run it for you.  Then an interface generator (swig) should be used to export the interface to Perl and PHP.