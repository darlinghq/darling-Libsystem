# Single kqueue registry

Compile kqueue-registry.c as a Mach-O against the configured Darling SDK and
libSystem. The public kqueue() entry must create a queue recognized by the
kqueue_close callback that libSystem supplies to the kernel, and the callback
must close its descriptor. This is a test of our own build invariant, not an
assertion about an Apple-private API. Baseline returns handled=0; a single-owner
build returns handled=1 and the descriptor is invalid afterward.

Provenance: own build definitions, existing libkqueue callback contract and
an authored API test (rung 4). No guest internals inspected. The two other
libkqueue OBJECTS inclusions are in darling-libc and darling-xnu; the kernel
image must retain the implementation, while libc and libSystem must resolve
to it. Build all affected first-pass and final images together.

The companion darling-libkqueue test/socket-independent-filters.c (#1274)
checks actual read/write delivery, close callbacks, surviving application
sockets after queue close, peer EOF after socket close and descriptor accounting.
Its independent-filter/teardown fix and this ownership fix are separate concerns
that both need to be present for the full companion matrix to pass.
