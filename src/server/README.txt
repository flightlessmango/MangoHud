Serialization concerns:

We use protocol buffers, because they are efficient, small, and
extensible, as well supported in many languages.

For client we stick to pure C implementation, in this case nanopb. The
nanopb is pure C implementation designed primarly for embeded systems,
but can be used on desktop. Primary reason we use pure C implementation,
is that we don't want to depend on C++ runtime, which might have
different ABI in overlay vs in the app we are dynamically linking with.

For server we can use different implementation of protocol buffers,
including use C++, and official Google's protocol buffer implementation
which is easy to use. However to reduce number of dependencies and share
common code between client and server, we stick to nanopb for server too.

In client we want to use non-blocking reads and writes, so we can't just
use read/write, of fread, or send in the decoding / encoding, as once the
encoding or decoding was requested, we can't stop and then resume. Only
option is to block, or pray we will get whole message in time. But that
can't be guaranteed, and would require failing to parse message (and all
subsequent messages due to frameing desynchronization), or require to
block, which would negatively impact frametimes.

Options are either to use decoding and encoding in client in a separat
thread, or encode into memory whole message, then send the encoded
message in non-blocking fashion. If the socket buffers are full, or write
would block, just keep track of bytes written so far, and retry on a next
frame. Similarly for reads, read how much we need to read, and if we
can't read enough, continue on a next frame. Once whole message is read,
trigger decode from memory.

Server can safely use blocking with each client handled in own thread,
but in the current version, we use same logic as in client, and use
non-blocking reads and writes, with single thread multiplexing using
epoll. This complicates things a bit, and requires using more memory
(temporary memory buffer + decoded message), but is relatively high
performnt and makes sure we use basically same code in client and server.

For the above to work, we use own message frameing, instead of nanopb
DELIMATED option. We just send 4 bytes with the size of the message,
before each message.

Additionally to reduce syscalls, we use C fread / fwrite, which do
internal buffering.

Note, that some older POSIX and ISO C standards didn't specify exact
behaviour of nonblocking IO on fread / fwrite, i.e. they didn't clarify
if errno is set to EAGAIN or anything like that. But IEEE Std
1003.1-2017, does state errno is set, so does GNU Gnulib libc
documentation (but not manpages). IEEE 1003.1-2017 states that errno is a
ISO C extension. However it is safe to use errno on POSIX.1-2017 systems.
ISO/IEC 9899:1999 specification. p. 301, § 7.19.8.1. as well ISO C17 are
silent about errno.

See:

https://www.gnu.org/software/gnulib/manual/html_node/fread.html
https://pubs.opengroup.org/onlinepubs/9699919799/functions/fread.html
http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf  (Comittee Draft, ISO/IEC 9899:1999 / TC2)
http://www.open-std.org/jtc1/sc22/WG14/www/docs/n1256.pdf  (end draft, ISO/IEC 9899:1999)

http://www.open-std.org/jtc1/sc22/WG14/www/docs/n1570.pdf  (public draft of C11 aka ISO/IEC 9899:2011)
https://web.archive.org/web/20181230041359if_/http://www.open-std.org/jtc1/sc22/wg14/www/abq/c17_updated_proposed_fdis.pdf
https://web.archive.org/web/20181230041359if_/http://www.open-std.org/jtc1/sc22/wg14/www/abq/c17_updated_proposed_fdis.pdf (latest free draft, ISO/IEC 9899:2018 aka C17 aka C18)


Additionally we don't want to generate spourious signals, or do modifications
of signal handlers in the app the client is running. This includes things
like SIGPIPE. Instead defer these to read/write phases, by using SO_NOSIGPIPE
on BSD, and MSG_NOSIGPIPE on Linux.


Portability:

  We use '%z' and '%zu' in fprintf formats. This is C99 feature.
  But MSVC only supports this from VS2013 (`_MSC_VER >= 1800`).
  We only these for debugging.

  We use UNIX sockets, but code is modular to be usable with TCP and SCTP
  sockets. The API doesn't expose these directly to the user.

