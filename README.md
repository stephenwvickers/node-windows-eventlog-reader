
# windows-eventlog-reader - [homepage][homepage]

This modules provides [Node.js][nodejs] programs the ability to read events
from a Windows Event Log.

This module is installed using [node package manager (npm)][npm]:

    # This module contains C++ source code which will be compiled
    # during installation using node-gyp.  A suitable build chain
    # must be configured before installation.
    
    npm install windows-eventlog-reader

It is loaded using the `require()` function:

    var eventlog = require ("windows-eventlog-reader");

Individual logs can then be opened and tail'ed:

    var reader = eventlog.createReader ("Application");
    
    function feedCb (event) {
        console.dir (event);
    }
    
    function doneCb (error) {
        if (error)
            console.error (error.toString ());
    }
    
    reader.readAll (1, feedCb, doneCb);
    
    reader.tail (1, function (error, event) {
        if (error)
            console.error (error.toString ());
        else
            console.dir (event);
    });

[homepage]: http://re-tool.org "Homepage"
[nodejs]: http://nodejs.org "Node.js"
[npm]: https://npmjs.org/ "npm"

# Non-Blocking Operations

Windows event logs are typically stored in files on a hard disk.  Opening and
reading an event log involves reading these files from disk, and the Windows
API to do so operates in a blocking mode, i.e. each read request will block
the calling program from performing other operations.  In the context of
[Node.js][nodejs] this will introduce a pause of an undefined amount of time
into the [Node.js][nodejs] event loop.

**NOTE** Although event log contents may come directly from the operating
systems file/page cache, the blocking I/O side effect is still passed on to
the calling program, i.e. something in the call chain may decide to block and
a calling [Node.js][nodejs] program cannot do anything about it.  Also, to
obtain formatted messages for each event log event, one or more DLL's need to
be loaded directly from disk, which involves more blocking I/O.

This module utilises the `libuv` library to integrate into the
[Node.js][nodejs] event loop - this library is also used by [Node.js][nodejs].
The `libuv` function `uv_queue_work()` is used to queue operations to run in
the background.  As a result this module will not block the [Node.js][nodejs]
event loop when blocking operations are performed.

There is one exception to this claim.  The `close()` method exposed by
this module is performed within the [Node.js][nodejs] event loop.  This
function will call the Win32 API function `CloseEventLog()`, which might
block.  However during development this function always seemed to return
pretty much instantly.

[nodejs]: http://nodejs.org "Node.js"

# Event Objects

This module uses JavaScript objects to represent events, for example:

    {
        sourceName: 'gupdate',
        computerName: 'as00041.uk.internal',
        recordNumber: 6330,
        timeGenerated: 1372576636,
        timeWritten: 1372576636,
        eventType: 'Information',
        eventCategory: 0,
        message: 'Service started'
    }

Each event object will have following parameters:

 * `sourceName` - String specifying the event source for the event, e.g.
   `MSSQLServer`
 * `computerName` - String specifying the computer on which the event was
   generated, e.g. `sql-server.test.com`
 * `recordNumber` - Identifies the event in this event log
 * `timeGenerated` - Instance of the JavaScript `Date` class specifying the
   date and time at which the event was generated
 * `timeWritten` - Instance of the JavaScript `Date` class specifying the
   date and time at which the event was actually written to the event log
 * `eventType` - String specifying event type, this can be one of `Error`,
   `Warning`, `Information`, `AuditSuccess`, `AuditFailure` or `Unknown`
 * `eventCategory` - Number specifying the event category, this is specific
   to the event and not defined by this module
 * `message` - String containing the formatted event message

# Error Handling

Each operation exposed by this module typically requires a mandatory callback
function which will be called when an operation completes.

Callback functions are typically provided an `error` argument, and almost all
errors are instances of the `Error` class.

In the event a Windows event log is cleared while a reader is reading from it
the Win32 API will return the error code `ERROR_EVENTLOG_FILE_CHANGED`.  In
this case the `error` argument will be an instance of the
`eventlog.EventLogClearedError` class.

This type of error will typically only occur for operations that involve a
read from the event log, e.g. `read()` or `tail()`.

If this error is experienced, the event log can be closed, re-opened, and
the original request re-submitted:

    var offset = 1;

    function cb (error) {
        if (error) {
            // When an error occurs this operation will complete, and in the
            // case of an event log being cleared we want to re-open it and
            // re-submit our event
            if (error instanceof eventlog.EventLogClearedError) {
                eventlog.close ();
                eventlog.open (function (error) {
                    if (error)
                        console.error (error.toString ());
                    else
                        eventlog.tail (offset, cb);
                });
            } else {
                console.error (error.toString ());
            }
        } else {
            offset = event.recordNumber;
            console.dir (event);
        }
    }

    eventlog.tail (offset, cb);

# Using This Module

Event log readers are represented by an instance of the `Reader` class.  This
module exports the `createReader()` function which is used to create instances
of the `Reader` class.

Readers can be used to read all or part of an event log, and to tail the event
log and notify users of new events.

## eventlog.createReader (name)

The `createReader()` function instantiates and returns an instance of the
`Reader` class:
    
    var reader = eventlog.createReader ("Application");

The `name` parameter is the name of the event log to create a reader for.

An exception will be thrown if the reader could not be created.  The error
will be an instance of the `Error` class.

After calling this function and creating a reader the underlying Windows event
log will not have been opened.  The `open()` method must be called on the
reader for the underlying Windows event log to be opened.

**NOTE** When the `open()` method is called this module will use the Win32 API
`OpenEventLog()` function.  The documentation for this function states that if
the event log could not be found then the `Application` event log will be
opened instead.

## reader.on ("close", callback)

The `close` event is emitted by the reader when the underlying event log
handle is closed.

No arguments are passed to the callback.

The following example prints a message to the console when the reader is
closed:

    reader.on ("close", function () {
        console.log ("event log closed");
    });

## reader.open (callback)

The `open()` method opens the underlying Windows event log represented by the
reader.

The `callback` function is called once the underlying Windows event log has
been opened.  The following arguments will be passed to the `callback`
function:

 * `error` - Instance of the `Error` class or a sub-class, or `null` if no
   error occurred

The following example opens the `Application` Windows event log:

    reader.open (function (error) {
        if (error)
            console.error (error.toString ());
    });

## reader.read (offset, feedCallback, doneCallback)

The `read()` method reads zero or more events from the underlying Windows
event log.

The `offset` parameter specifies at what record number to start reading from
(note that the event with this record number will also be returned if it
exists).

This method will not call a single callback once some events have been read.
Instead the `feedCallback` function will be called for each event read.  The
following arguments will be passed to the `feedCallback` function:

 * `event` - An object describing the event (see the "Event Objects" section
   for more information on event objects)

Once the read has completed, or an error has occurred, the `doneCallback`
function will be called.  The following arguments will be passed to the
`doneCallback` function:

 * `error` - Instance of the `Error` class or a sub-class, or `null` if no
   error occurred

Once the `doneCallback` function has been called the read is complete and
the `feedCallback` function will no longer be called.

The following example reads a number of events starting at offset 1,000:

    function feedCb (event) {
        console.dir (event);
    }

    function doneCb (error) {
        if (error)
            console.error (error.toString ());
    }

    var reader = eventlog.createReader (name);
    var offset = 1000;

    reader.open (function (error) {
        if (error) {
            console.error (error.toString ());
        } else {
            reader.read (offset, feedCb, doneCb);
        }
    });

## reader.readAll (offset, feedCallback, doneCallback)

The `readAll()` method reads zero or more events from the underlying Windows
event log until no more events are available.

The `offset` parameter specifies at what record number to start reading from
(note that the event with this record number will also be returned if it
exists).

This method will not call a single callback once all events have been read.
Instead the `feedCallback` function will be called for each event read.  The
following arguments will be passed to the `feedCallback` function:

 * `event` - An object describing the event (see the "Event Objects" section
   for more information on event objects)

Once the read has completed, or an error has occurred, the `doneCallback`
function will be called.  The following arguments will be passed to the
`doneCallback` function:

 * `error` - Instance of the `Error` class or a sub-class, or `null` if no
   error occurred

Once the `doneCallback` function has been called all available events will
have been read and the `feedCallback` function will no longer be called.

The following example reads all events starting at offset 2,000:

    function feedCb (event) {
        console.dir (event);
    }

    function doneCb (error) {
        if (error)
            console.error (error.toString ());
    }

    var reader = eventlog.createReader (name);
    var offset = 2000;

    reader.open (function (error) {
        if (error) {
            console.error (error.toString ());
        } else {
            reader.readAll (offset, feedCb, doneCb);
        }
    });

## reader.tail (offset, [interval], callback)

The `tail()` method periodically reads zero or more events from the underlying
Windows event log as they become available.

The `offset` parameter specifies at what record number to start reading from
(note that the event with this record number will also be returned if it
exists).  The optional `interval` parameter is the number of milliseconds
between each read call, and defaults to `3000`.

The `callback` function will be called as each event is read from the event
log, or if an error occurs.  The following arguments will be passed to the
`callback` function:

 * `error` - Instance of the `Error` class or a sub-class, or `null` if no
   error occurred
 * `event` - An object describing the event (see the "Event Objects" section
   for more information on event objects)

**NOTE** If an error occurs the `callback` function will be called with the
first `error` argument defined, and after the `callback` function returns the
tail operation will be stopped.

The following example reads all events starting at offset 6,000, and then
tails the Windows event log, calling the `cb` function as and when events
become available:

    function cb (error, event) {
        if (error)
            console.error (error.toString ());
        else
            console.dir (event);
    }

    var reader = eventlog.createReader (name);
    var offset = 6000;

    reader.open (function (error) {
        if (error) {
            console.error (error.toString ());
        } else {
            reader.tail (offset, cb);
        }
    });

# Example Programs

Example programs are included under the modules `example` directory.

# Bugs & Known Issues

None, yet!

Bug reports should be sent to <stephen.vickers.sv@gmail.com>.

# Changes

## Version 1.0.0 - 30/06/2013

 * Initial release

## Version 1.0.1 - 01/07/2013

 * Add "Error Handling" section to the README.md file

# Roadmap

In no particular order:

 * Make `close()` operations non-blocking (line `open()`, `read()`, etc.)
 * Allow the `read()` and `readAll()` `feedCallbacks` to indicate reading
   should stop by returning some value (i.e. something like `true`)
 * Throw an exception when the event log name provided is not valid

Suggestions and requirements should be sent to <stephen.vickers.sv@gmail.com>.

# License

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see
[http://www.gnu.org/licenses](http://www.gnu.org/licenses).

# Author

Stephen Vickers <stephen.vickers.sv@gmail.com>
