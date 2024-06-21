[![SnertSoft: We Serve Your Server](Img/logo-300x74.png)](http://software.snert.com/)

milter-length  « When Size Matters »
====================================

Copyright 2004, 2024 by Anthony Howe.  All rights reserved.


WARNING
-------

THIS IS MAIL FILTERING SOFTWARE AND WILL BLOCK MAIL THAT FAILS TO PASS A GIVEN SET OF TESTS.  SNERTSOFT AND THE AUTHOR DO NOT ACCEPT ANY RESPONSIBLITY FOR MAIL REJECTED OR POSSIBLE LOSS OF BUSINESSS THROUGH THE USE OF THIS SOFTWARE.  BY INSTALLING THIS SOFTWARE THE CLIENT UNDERSTANDS AND ACCEPTS THE RISKS INVOLVED.


Description
-----------


This is a [Sendmail](http://www.sendmail.org/) utility milter that imposes message size limits by IP address, domain name, or sender address on a message body length, excluding the message headers.  Sendmail's `MaxMessageSize` option only allows for a single global server wide message size limit, which is insufficient for some sites that would prefer finer granularity in the application of message size limits.  This is particularly useful for mail hosts that manage several domains and/or a large number of users, such as an ISP.

Note that if the Sendmail option `MaxMessageSize` is specified in the sendmail.cf configuration, then that will impose an upper limit to any `milter-length` message size limits. 


Usage
-----

        milter-length [options ...] [arguments ...]

Options can be expressed in four different ways.  Boolean options are expressed as `+option` or `-option` to turn the option on or off respectively.  Options that required a value are expressed as `option=value` or `option+=value` for appending to a value list.  Note that the `+option` and `-option` syntax are equivalent to `option=1` and `option=0` respectively.  Option names are case insensitive.

Some options, like `+help` or `-help`, are treated as immediate actions or commands.  Unknown options are ignored.  The first command-line argument is that which does not adhere to the above option syntax.  The special command-line argument `--` can be used to explicitly signal an end to the list of options.

The default options, as shown below, can be altered by specifying them on the command-line or within an option file, which simply contains command-line options one or more per line and/or on multiple lines.  Comments are allowed and are denoted by a line starting with a hash (#) character.  If the `file=` option is defined and not empty, then it is parsed first followed by the command-line options.

Note that there may be additional options that are listed in the option summary given by `+help` or `-help` that are not described here.


- - -
### access-db=/etc/mail/access.db

The type and location of the read-only access key-value map.  It provides a centralised means to black and white list hosts, domains, mail addresses, etc.  The following methods are supported:

        text!/path/map.txt                      R/O text file, memory hash
        /path/map.db                            Berkeley DB hash format
        db!/path/map.db                         Berkeley DB hash format
        db!btree!/path/map.db                   Berkeley DB btree format
        sql!/path/database                      An SQLite3 database
        socketmap!host:port                     Sendmail style socket-map
        socketmap!/path/local/socket            Sendmail style socket-map
        socketmap!123.45.67.89:port             Sendmail style socket-map
        socketmap![2001:0DB8::1234]:port        Sendmail style socket-map

If `:port` is omitted, the default is `7953`.

The `access-db` contains key-value pairs.  Lookups are performed from most to least specific, stopping on the first entry found.  Keys are case-insensitive.

An IPv4 lookup is repeated several times reducing the IP address by one octet from right to left until a match is found.

        tag:192.0.2.9
        tag:192.0.2
        tag:192.0
        tag:192

An IPv6 lookup is repeated several times reducing the IP address by one 16-bit word from right to left until a match is found.

        tag:2001:0DB8:0:0:0:0:1234:5678
        tag:2001:0DB8:0:0:0:0:1234
        tag:2001:0DB8:0:0:0:0
        tag:2001:0DB8:0:0:0
        tag:2001:0DB8:0:0
        tag:2001:0DB8:0:0
        tag:2001:0DB8:0
        tag:2001:0DB8
        tag:2001

A domain lookup is repeated several times reducing the domain by one label from left to right until a match is found.

        tag:[ipv6:2001:0DB8::1234:5678]
        tag:[192.0.2.9]
        tag:sub.domain.tld
        tag:domain.tld
        tag:tld
        tag:

An email lookup is similar to a domain lookup, the exact address is first tried, then the address's domain, and finally the local part of the address.

        tag:account@sub.domain.tld
        tag:sub.domain.tld
        tag:domain.tld
        tag:tld
        tag:account@
        tag:

If a key is found and is a milter specific tag (ie. `milter-length-Connect`, `milter-length-To`), then the value is processed as a pattern list and the result returned.  The Sendmail variants cannot have a pattern list.  A pattern list is a whitespace separated list of _pattern-action_ pairs followed by an optional default _action_.  The supported patterns are:

        [network/cidr]size            Classless Inter-Domain Routing
        !pattern!size                 Simple fast text matching.
        /regex/size                   POSIX Extended Regular Expressions

The CIDR will only ever match for IP address related lookups.

A `!pattern!` uses an astrisk (\*) for a wildcard, scanning over zero or more characters; a question-mark (?) matches any single character; a backslash followed by any character treats it as a literal (it loses any special meaning).

        !abc!           exact match for 'abc'
        !abc*!          match 'abc' at start of string
        !*abc!          match 'abc' at the end of string
        !abc*def!       match 'abc' at the start and match 'def' at the end, maybe with stuff in between.
        !*abc*def*!     find 'abc', then find 'def'

Below is a list of supported tags.  Other options may specify additional tags.

        milter-length-Connect:client-ip          size           § Can be a pattern list.
        milter-length-Connect:[client-ip]        size           § Can be a pattern list.
        milter-length-Connect:client-domain      size           § Can be a pattern list.
        milter-length-Connect:                   size           § Can be a pattern list.

        milter-length-Auth:auth_authen           size           § Can be a pattern list.
        milter-length-Auth:                      size           § Can be a pattern list.

        milter-length-From:sender-address        size           § Can be a pattern list.
        milter-length-From:sender-domain         size           § Can be a pattern list.
        milter-length-From:sender@               size           § Can be a pattern list.
        milter-length-From:                      size           § Can be a pattern list.

        milter-length-To:recipient-address       size           § Can be a pattern list.
        milter-length-To:recipient-domain        size           § Can be a pattern list.
        milter-length-To:recipient@              size           § Can be a pattern list.
        milter-length-To:                        size           § Can be a pattern list.

The _size_ is the maximum number of octets permitted in a message and is expressed as a number with an optional scale suffix: K (kilo), M (mega), or G (giga).  If no size is specified (or -1), then the message can be any length (ULONG_MAX).

When there are multiple message size limits assigned, then the limit applied, in order of precedence, will be: the maximum value of all relevant `milter-length-to:`, `milter-length-auth:`, `milter-length-from:`, `milter-length-connect:`, or an unlimited default.  Essentially each matching milter tag replaces the previous value.  When there are multiple recipients with one or more possible `milter-length-to:` entries, then maximum length limit found is applied. 

Below are only some examples of what is possible: 

        milter-length-Connect:80.94             [80.94.96.0/20]2M  500K
 
All the mail from netblock 80.94.96.0/20 (80.94.96.0 through to 80.94.111.255) is limited to 2 megabytes, while anything else in 80.94.0.0/16 is limited to 500 kilobytes.
 
        milter-length-Connect:192.0.2           10M
 
All the mail from the network in 192.0.2.0/24 is limited to 10 megabytes.
 
        milter-length-Connect:192.0.2           /^192\.0\.2\.8[0-9]/1M  10M

Mail sent from IP addresses 192.0.2.80 through to 192.0.2.89 is limited to 1 megabyte, while the rest of the network in 192.0.2.0/24 is limited to 10 megabytes.  This example is rather contrived to show some of what is possible with patterns and is the equivalent of ten simple `milter-length-connect:` entries.  When using patterns, weigh the need verses simplicity, readability, and administration.
 
        milter-length-From:example.com          /^[^+]+@/500K
 
Mail from within example.com, where the sender's address does not contain a plus-detail, is limited to 500 kilobytes, otherwise no limit is imposed.
 
        milter-length-To:example.net            !*+*@*!  /^[0-9].*/20K  !*smith*@*!1M 4M
 
Mail to addresses within `example.net` containing a plus-detail have no limit; those starting with a digit are limited to 20 kilobytes; mail to anyone with "smith" as part of their address is limited to 1 megabyte, while the rest of the domain is limited to 4 megabytes. 


- - -
### +daemon

Start as a background daemon or foreground application.


- - -
### file=/etc/mail/milter-length.cf

Read the option file before command line options.  This option is set by default.  To disable the use of an option file, simply say `file=''`.


- - -
### -help or +help

Write the option summary to standard output and exit.  The output is suitable for use as an option file.


- - -
### milter-socket=unix:/var/run/milter/milter-length.socket

A socket specifier used to communicate between Sendmail and `milter-length`.  Typically a unix named socket or a host:port.  This value must match the value specified for the `INPUT_MAIL_FILTER()` macro in the sendmail.mc file.  The accepted syntax is:

        {unix|local}:/path/to/file              A named pipe. (default)
        inet:port@{hostname|ip-address}         An IPV4 socket.
        inet6:port@{hostname|ip-address}        An IPV6 socket.


- - -
### milter-timeout=7210

The sendmail/milter I/O timeout in seconds.


- - -
### pid-file=/var/run/milter/milter-length.pid

The file path of where to save the process-id.


- - -
### -quit or +quit

Quit an already running instance of the milter and exit.  This is equivalent to:

        kill -QUIT `cat /var/run/milter/milter-length.pid`.


- - -
### -restart or +restart

Terminate an already running instance of the milter before starting.


- - -
### run-group=milter

The process runtime group name to be used when started by root.

- - -
### run-user=milter

The process runtime user name to be used when started by root.


- - -
### -smtp-auth-ok

Allow SMTP authenticated senders to send unscanned mail.  See also the `milter-length-auth:` tag (`access-db=`) for finer granularity of control.


- - -
### verbose=info

A comma separated list of how much detail to write to the mail log.  Those mark with `§` have meaning for this milter.

        §  all          All messages
        §  0            Log nothing.
        §  info         General info messages. (default)
        §  trace        Trace progress through the milter.
        §  parse        Details from parsing addresses or special strings.
           debug        Lots of debug messages.
           dialog       I/O from Communications dialog
           state        State transitions of message body scanner.
           dns          Trace & debug of DNS operations
           cache        Cache get/put/gc operations.
        §  database     Sendmail database lookups.
           socket-fd    socket open & close calls
           socket-all   All socket operations & I/O
        §  libmilter    libmilter engine diagnostics


- - -
### work-dir=/var/tmp

The working directory of the process.  Normally serves no purpose unless the kernel option that permits daemon process core dumps is set.


SMTP Responses
--------------

This is the list of possible SMTP responses:

* 550 5.7.1 max. message size \([0-9]+\) exceeded  
  The message body, not counting the headers, is too big.
    
* 553 5.1.0 imbalanced angle brackets in path  
  The path given for a `MAIL` or `RCPT` command is missing a closing angle bracket

* 553 5.1.0 address does not conform to RFC 2821 syntax  
  The address is missing the angle brackets, `<` and `>`, as required by the RFC grammar.

* 553 5.1.0 local-part too long  
  The stuff before the `@` is too long.

* 553 5.1.[37] invalid local part  
  The stuff before the `@` sign contains unacceptable characters.

* 553 5.1.0 domain name too long  
  The stuff after the `@` is too long.

* 553 5.1.7 address incomplete  
  Expecting a domain.tld after the `@` sign and found none.

* 553 5.1.[37] invalid domain name  
  The domain after the `@` sign contains unacceptable characters.


Build & Install
---------------

* Install `SQLite` from a package if desired.  Prior to [LibSnert's](https://github.com/SirWumpus/libsnert) availability on GitHub, the old `libsnert` tarballs included SQLite, but the GitHub [libsnert](https://github.com/SirWumpus/libsnert) repository does not, so it needs to be installed separately.   `milter-length` does not require it, but other milters that need a cache will.

* If you have never built a milter for Sendmail, then please make sure that you build and install `libmilter` (or install a pre-built package), which is _not_ built by default when you build Sendmail.  Please read the `libmilter` documentation.  Briefly, it should be something like this:

        cd (path to)/sendmail-8.13.6/libmilter
        sh Build -c install

* [Build LibSnert](https://github.com/SirWumpus/libsnert#configuration--build) first, do *not* disable `sqlite3` support; it should find the pre-installed version of SQLite if any.

* Building `milter-length` should be:

        cd com/snert/src
        git clone https://github.com/SirWumpus/milter-length.git
        cd milter-length
        ./configure --help
        ./configure
        make
        sudo make install

* An example `/usr/local/share/examples/milter-length/milter-length.mc` is supplied.  This file should be reviewed and the necessary elements inserted into your Sendmail `.mc` file and `sendmail.cf` rebuilt.  Please note the comments on the general milter flags.

* Once installed and configured, start `milter-length` and then restart Sendmail.  An example startup script is provided in `/usr/local/share/examples/milter-length/milter-length.sh`.


Notes
-----

* The minimum desired file ownership and permissions are as follows for a typical Linux system.  For FreeBSD, NetBSD, and OpenBSD the binary and cache locations may differ, but have the same permissions.  Process user `milter` is primary member of group `milter` and secondary member of group `smmsp`.  Note that the milter should be started as `root`, so that it can create a _.pid file_ and _.socket file_ in `/var/run`; after which it will switch process ownership to `milter:milter` before starting the accept socket thread.

        /etc/mail/                              root:smmsp      0750 drwxr-x---
        /etc/mail/access.db                     root:smmsp      0640 -rw-r-----
        /etc/mail/sendmail.cf                   root:smmsp      0640 -rw-r-----
        /etc/mail/milter-length.cf              root:root       0644 -rw-r--r--
        /var/run/milter/milter-length.pid       milter:milter   0644 -rw-r--r--
        /var/run/milter/milter-length.socket    milter:milter   0644 srw-r--r--
        /var/db/milter-length                   milter:milter   0644 -rw-r--r-- (*BSD)
        /var/cache/milter-length                milter:milter   0644 -rw-r--r-- (linux)
        /usr/local/libexec/milter-length        root:milter     0550 -r-xr-x---
