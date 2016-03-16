## Release Tests ##

| **Date** | **Machine** | **Architecture** | **OS** | **Apache** | **MySQL** | **Result** | **Notes** |
|:---------|:------------|:-----------------|:-------|:-----------|:----------|:-----------|:----------|
| 11/3     | Oracle Enterprise Linux 5 / xen | i386             | 2.6.18-8.el5xen | 2.2.3      |  6.3.20   | OK         | (Aslo for rhel 5 and centos 5):  (1) yum install httpd-devel (2) Install MySQL-Cluster-gpl-devel RPM from mysql.com (3) ./configure --apxs=/usr/sbin/apxs  |
| 7/21     | SGI Altix XE - 4 node (each node w/ 23GiB, 4 2.26Ghz/Quad-Core Xeon E5520) | x86\_64          | SUSE Linux Enterprise Server 10 | TBD        | TBD       | TBD        | TBD       |
| 6/24     | Centos 5 (vm) | i386             | Centos 5 | 2.2.3      | 7.0.6     | Undefined symbol clock\_gettime (fixed in [r650](https://code.google.com/p/mod-ndb/source/detail?r=650)) | With RPMs: httpd-2.2.3-22 , httpd-devel-2.2.3-22, mysql-cluster-gpl-devel-7.0.6-0.  Configure mod\_ndb with: `./configure --apxs=/usr/sbin/apxs`  |
| 6/23     | tdm         | x86\_64          | Mac OS X 10.5 Server | 2.2.11 (worker) | 7.0.6     | OK         |With [mysql-cluster-gpl-7.0.6-osx10.5-x86\_64.dmg](http://dev.mysql.com/downloads/cluster/7.0.html#Mac_OS_X) Configure Apache with: export CFLAGS="-m64"; export CPPFLAGS="-m64"; ./configure --enable-layout="Darwin" --with-mpm=worker --enable-so Configure mod\_ndb with:./configure --mysql=/usr/local/mysql/bin/mysql\_config --apxs=/usr/sbin/apxs  --64|
| 6/22     | sam         | i386             | Mac OS X 10.5 | 2.2.8 (worker) | 7.0.5     | OK         |           |
| 6/22     | sam         | amd64            | Mac OS X 10.5 | 2.2.11 (prefork) | 7.0.6     | OK         | With [mysql-cluster-gpl-7.0.6-osx10.5-x86\_64.tar.gz ](http://dev.mysql.com/downloads/cluster/7.0.html).  Configure mod\_ndb with:  `./configure --64` |
| 5/22     | sam         | i386             | Mac OS X 10.5 | 1.3.37     | 7.0.5     | OK         |           |
| 6/22     | christmas   | amd64            | opensolaris |  2.2       | 6.3.20    | OK         | Sun Studio 12 |
| 6/22     | centos1 (vm) | i386             | Centos 4 zone | 1.3        | 7.0.5     | OK         |           |
| 6/22     | ubuntu8 (vm) | i386             | Ubuntu 8.04 LTS | 2.2.8      | 7.0.5     | OK         | With [mysql-cluster-gpl-7.0.5-linux-i686-glibc23.tar.gz](http://dev.mysql.com/downloads/cluster/7.0.html) plus these packages: g++, curl, apache2, apache2-mpm-worker, apache2-threaded-dev, httperf  Configure mod\_ndb with: `./configure --apxs=/usr/bin/apxs2` |