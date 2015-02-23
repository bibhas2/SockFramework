##Which Events Occur?
1. **Basic I/O events: ** The socket is readable or writable.
2. **``connect()`` completion event: ** After an asynchrounous ``connect()`` completes or fails. This is signalled in two different ways.
3. **Orderly disconnect by the server: ** This happens after an HTTP server closes a keep alive connection.
4. **Unexpected network problem: ** This can happen due to network connectivity problem.

###Basic I/O Event
This is signalled by the ``onReadable`` and ``onWritable`` method of a 
``SocketRec`` being called. 

A socket becomes unwritable in a situation when a large amount of
data is queued to be written to it. Otherwise, a socket is generally
writable. This means ``onWritable`` will be continuously called
even when the application has no need to write data. To prevent
that set the ``needToWrite`` property of a ``SocketRec`` to ``0``.
The pump does not select for writable condition when ``needToWrite``
is ``0``. This will obviously avoid a busy wait condition and improve
performance.

###Asynchronous Connect Completion
An asynch ``connect()`` call signals its completion by posting a
writable event for the socket. From my experience, both readable and
writable event is fired for the socket (in Linux and Mac). One needs to
then obtain the ``SO_ERROR`` option of the socket using ``getsockopt()``
to determine if the connection completed with error or successfully.

Applications must keep track of if connection has indeed completed
before trying to read or write from the ``onReadable`` and
``onWritable`` methods.

Unfortunately, in case of an error to connect a long time may pass between 
the calling of ``connect()`` and posting of the readable event. Most
applications should not wait that long. They should use the
``onTimeout`` method to give up on trying to connect. From the 
``onTimeout`` method check to see if connection has been already made.
If so, there is no harm done. If not, it is the connection that must
have timed out.

###Orderly Disconnect by the Server
This event happens when the server calls ``close()`` to close a
socket.  When that happens a readable event is posted for the socket
in the client side. But when the client tries to read a length of
``0`` bytes is returned. Hence, a return value of ``0`` from
``read()`` indicates orderly disconnect by the server.

In HTTP the same connection can be used to send multiple requests 
(this is called keep alive). However the server can close the connection 
at any time without any prearranged agreement with the client. The
client should detect orderly disconnect by checking the return value of
``read()`` from ``onReadable``. Also note, ``onReadable`` may be called
to signal orderly disconnect even if the client is not expecting any
response from the server.


###Unexpected Network Problem

