/*
 * Darling shim for ___chkstk_darwin.
 *
 * Modern macOS clang/swift emits calls to ___chkstk_darwin for large stack
 * frames; the real implementation lives in libsystem_pthread.dylib but is
 * not re-exported through libSystem.B.dylib (the entry point that user
 * binaries bind against). Provide the symbol directly here so libSystem
 * carries the export.
 *
 * Behavior: on aarch64 we probe each 4 KiB page from sp down by x9 bytes
 * — same shape as Apple's stub, minus the pthread_self() bounds check
 * (Darling arm64 has no TPIDRRO_EL0-backed Darwin TSD, so we cannot read
 * stackaddr/stackbottom cheaply here). Linux grows the stack on page-fault
 * automatically, so a missing bounds check just means stack overflow becomes
 * a normal SIGSEGV instead of an early crash. For x86_64 we emit a plain
 * `ret` — those builds already worked fine without this symbol; we only
 * needed it on arm64 where newer-macOS toolchains emit the call.
 */

/* Apple's ABI for this symbol uses 3 underscores in the Mach-O symbol table
 * (i.e. the C declaration is `void __chkstk_darwin(void)` — two underscores
 * in C source, with the standard C→asm leading underscore making it three).
 * libpthread's pthread_asm.S declares ____chkstk_darwin with 4 underscores,
 * which corresponds to a C symbol named ___chkstk_darwin — that's a separate
 * name and doesn't satisfy clang's bind that references ___chkstk_darwin
 * directly. We export 3-underscore names here. */
#if defined(__aarch64__) || defined(__arm64__)
	.text
	.align 2
	.globl ___chkstk_darwin
___chkstk_darwin:                   // x9 = alloca size
	cbz     x9, Lcsd_end
	mov     x10, sp
Lcsd_loop:
	cmp     x9, #0x1000
	b.lo    Lcsd_end
	sub     x10, x10, #0x1000
	ldr     x11, [x10]              // touch the page
	sub     x9, x9, #0x1000
	b       Lcsd_loop
Lcsd_end:
	ret

	.globl _thread_chkstk_darwin
_thread_chkstk_darwin:
	b       ___chkstk_darwin
#elif defined(__x86_64__)
	.text
	.align 4
	.globl ___chkstk_darwin
___chkstk_darwin:
	ret
	.globl _thread_chkstk_darwin
_thread_chkstk_darwin:
	jmp     ___chkstk_darwin
#endif
