/*
 * Copyright (c) 2007, 2008, 2011-2013 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <TargetConditionals.h>	// for TARGET_OS_*

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <corecrypto/cc_priv.h>
#include <libc_private.h>
#include <pthread.h>
#include <pthread/private.h>
#if !TARGET_OS_DRIVERKIT
#include <dlfcn.h>
#include <os/variant_private.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include <sys/kdebug.h>
#include <_libkernel_init.h> // Must be after voucher_private.h
#include <malloc_implementation.h>

#include <mach-o/dyld_priv.h>

#ifdef DARLING
extern int _dyld_func_lookup(const char* name, void** address);
#endif

// system library initialisers
extern void mach_init(void);			// from libsystem_kernel.dylib
extern void __libplatform_init(void *future_use, const char *envp[], const char *apple[], const struct ProgramVars *vars);
extern void __pthread_init(const struct _libpthread_functions *libpthread_funcs, const char *envp[], const char *apple[], const struct ProgramVars *vars);	// from libsystem_pthread.dylib
extern void __pthread_late_init(const char *envp[], const char *apple[], const struct ProgramVars *vars);	// from libsystem_pthread.dylib
extern void __malloc_init(const char *apple[]); // from libsystem_malloc.dylib
extern void __keymgr_initializer(void);		// from libkeymgr.dylib
extern void _dyld_initializer(void);		// from libdyld.dylib
extern void libdispatch_init(void);		// from libdispatch.dylib
extern void _libxpc_initializer(void);		// from libxpc.dylib
extern void _libsecinit_initializer(void);        // from libsecinit.dylib
extern void _libtrace_init(void);		// from libsystem_trace.dylib
extern void _container_init(const char *apple[]); // from libsystem_containermanager.dylib
extern void __libdarwin_init(void);		// from libsystem_darwin.dylib


// clear qos tsd (from pthread)
extern void _pthread_clear_qos_tsd(mach_port_t) __attribute__((weak_import));

// system library atfork handlers
extern void _pthread_atfork_prepare(void);
extern void _pthread_atfork_parent(void);
extern void _pthread_atfork_child(void);
extern void _pthread_atfork_prepare_handlers();
extern void _pthread_atfork_parent_handlers(void);
extern void _pthread_atfork_child_handlers(void);
extern void _pthread_exit_if_canceled(int);

extern void dispatch_atfork_prepare(void);
extern void dispatch_atfork_parent(void);
extern void dispatch_atfork_child(void);

extern void _libtrace_fork_child(void);

extern void _malloc_fork_prepare(void);
extern void _malloc_fork_parent(void);
extern void _malloc_fork_child(void);

extern void _mach_fork_child(void);
#ifdef DARLING
extern void _mach_fork_parent(void);
#endif
extern void _notify_fork_child(void);
extern void _dyld_atfork_prepare(void);
extern void _dyld_atfork_parent(void);
extern void _dyld_fork_child(void);
extern void xpc_atfork_prepare(void);
extern void xpc_atfork_parent(void);
extern void xpc_atfork_child(void);
extern void _libSC_info_fork_prepare(void);
extern void _libSC_info_fork_parent(void);
extern void _libSC_info_fork_child(void);
extern void _asl_fork_child(void);

#if defined(HAVE_SYSTEM_CORESERVICES)
// libsystem_coreservices.dylib
extern void _libcoreservices_fork_child(void);
extern char *_dirhelper(int, char *, size_t);
#endif

#ifdef DARLING
extern void kqueue_atfork(void);
#endif

// advance decls for below;
void libSystem_atfork_prepare(void);
void libSystem_atfork_parent(void);
void libSystem_atfork_child(void);

#ifdef DARLING
void libSystem_posix_spawn_prepare(void);
void libSystem_posix_spawn_parent(void);
void libSystem_posix_spawn_child(void);
#endif

#if SUPPORT_ASAN
const char *__asan_default_options(void);
#endif

static inline void
_libSystem_ktrace4(uint32_t code, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	if (__builtin_expect(*(volatile uint32_t *)_COMM_PAGE_KDEBUG_ENABLE == 0, 1)) return;
	kdebug_trace(code, a, b, c, d);
}
#define _libSystem_ktrace3(code, a, b, c) _libSystem_ktrace4(code, a, b, c, 0)
#define _libSystem_ktrace2(code, a, b)    _libSystem_ktrace4(code, a, b, 0, 0)
#define _libSystem_ktrace1(code, a)       _libSystem_ktrace4(code, a, 0, 0, 0)
#define _libSystem_ktrace0(code)          _libSystem_ktrace4(code, 0, 0, 0, 0)

/*
 * these define stable Ariadne tracepoints. If initializers are removed, or
 * added, then old tracepoints MUST NOT be recycled.
 */
enum {
	ARIADNE_LIFECYCLE_libsystem_init = ARIADNEDBG_CODE(220, 4),
};

/*
 * These represent the initializer "name"
 *
 * They happen to match the order of the initializers at some point in time,
 * but there's no guarantee made that traecepoints will appear in numerical
 * order. As initializers come and go, new codes shall be allocated,
 * and no slots reused.
 */
enum init_func {
	INIT_SYSTEM = 0,
	INIT_KERNEL = 1,
	INIT_PLATFORM = 2,
	INIT_PTHREAD = 3,
	INIT_LIBC = 4,
	INIT_MALLOC = 5,
	INIT_KEYMGR = 6,
	INIT_DYLD = 7,
	INIT_LIBDISPATCH = 8,
	INIT_LIBXPC = 9,
	INIT_LIBTRACE = 10,
	INIT_SECINIT = 11,
	INIT_CONTAINERMGR = 12,
	INIT_DARWIN = 13,
};

#define _libSystem_ktrace_init_func(what) \
	_libSystem_ktrace1(ARIADNE_LIFECYCLE_libsystem_init | DBG_FUNC_NONE, INIT_##what)

// libsyscall_initializer() initializes all of libSystem.dylib
// <rdar://problem/4892197>
__attribute__((constructor))
static void
libSystem_initializer(int argc,
		      const char* argv[],
		      const char* envp[],
		      const char* apple[],
		      const struct ProgramVars* vars)
{
	static const struct _libkernel_functions libkernel_funcs = {
		.version = 4,
		// V1 functions
#if !TARGET_OS_DRIVERKIT
		.dlsym = dlsym,
#endif
		.malloc = malloc,
		.free = free,
		.realloc = realloc,
		._pthread_exit_if_canceled = _pthread_exit_if_canceled,
		// V2 functions (removed)
		// V3 functions
		.pthread_clear_qos_tsd = _pthread_clear_qos_tsd,
		// V4 functions
		.pthread_current_stack_contains_np = pthread_current_stack_contains_np,
#ifdef DARLING
		.dyld_func_lookup = _dyld_func_lookup,
		.posix_spawn_prepare = libSystem_posix_spawn_prepare,
		.posix_spawn_parent = libSystem_posix_spawn_parent,
		.posix_spawn_child = libSystem_posix_spawn_child,
#endif
	};

	static const struct _libpthread_functions libpthread_funcs = {
		.version = 2,
		.exit = exit,
		.malloc = malloc,
		.free = free,
	};

	static const struct _libc_functions libc_funcs = {
		.version = 1,
		.atfork_prepare = libSystem_atfork_prepare,
		.atfork_parent = libSystem_atfork_parent,
		.atfork_child = libSystem_atfork_child,
#if defined(HAVE_SYSTEM_CORESERVICES)
		.dirhelper = _dirhelper,
#endif
	};
	
	_libSystem_ktrace0(ARIADNE_LIFECYCLE_libsystem_init | DBG_FUNC_START);

	__libkernel_init(&libkernel_funcs, envp, apple, vars);
	_libSystem_ktrace_init_func(KERNEL);

	__libplatform_init(NULL, envp, apple, vars);
	_libSystem_ktrace_init_func(PLATFORM);

	__pthread_init(&libpthread_funcs, envp, apple, vars);
	_libSystem_ktrace_init_func(PTHREAD);

	_libc_initializer(&libc_funcs, envp, apple, vars);
	_libSystem_ktrace_init_func(LIBC);

	// TODO: Move __malloc_init before __libc_init after breaking malloc's upward link to Libc
	// Note that __malloc_init() will also initialize ASAN when it is present
	__malloc_init(apple);
	_libSystem_ktrace_init_func(MALLOC);

#if TARGET_OS_OSX
	/* <rdar://problem/9664631> */
	__keymgr_initializer();
	_libSystem_ktrace_init_func(KEYMGR);
#endif

	_dyld_initializer();
	_libSystem_ktrace_init_func(DYLD);

#if TARGET_OS_OSX
	__pthread_late_init(envp, apple, vars);
#endif

	libdispatch_init();
	_libSystem_ktrace_init_func(LIBDISPATCH);

#if !TARGET_OS_DRIVERKIT
	_libxpc_initializer();
	_libSystem_ktrace_init_func(LIBXPC);

#if SUPPORT_ASAN
	setenv("DT_BYPASS_LEAKS_CHECK", "1", 1);
#endif
#endif // !TARGET_OS_DRIVERKIT

	// must be initialized after dispatch
	_libtrace_init();
	_libSystem_ktrace_init_func(LIBTRACE);

#if !TARGET_OS_DRIVERKIT
#if defined(HAVE_SYSTEM_SECINIT)
	_libsecinit_initializer();
	_libSystem_ktrace_init_func(SECINIT);
#endif

#if defined(HAVE_SYSTEM_CONTAINERMANAGER)
	_container_init(apple);
	_libSystem_ktrace_init_func(CONTAINERMGR);
#endif

	__libdarwin_init();
	_libSystem_ktrace_init_func(DARWIN);
#endif // !TARGET_OS_DRIVERKIT

	const struct _malloc_late_init mli = {
		.version = 1,
#if !TARGET_OS_DRIVERKIT
		.dlopen = dlopen,
		.dlsym = dlsym,
		// this must come after _libxpc_initializer()
		.internal_diagnostics = os_variant_has_internal_diagnostics("com.apple.libsystem"),
#endif
	};

	__malloc_late_init(&mli);

#if !TARGET_OS_IPHONE
	/* <rdar://problem/22139800> - Preserve the old behavior of apple[] for
	 * programs that haven't linked against newer SDK.
	 */
#define APPLE0_PREFIX "executable_path="
	if (dyld_get_program_sdk_version() < DYLD_MACOSX_VERSION_10_11){
		if (strncmp(apple[0], APPLE0_PREFIX, strlen(APPLE0_PREFIX)) == 0){
			apple[0] = apple[0] + strlen(APPLE0_PREFIX);
		}
	}
#endif

#if TARGET_OS_OSX && !defined(__i386__)
	bool enable_system_version_compat = false;
	bool enable_ios_version_compat = false;
	char *system_version_compat_override = getenv("SYSTEM_VERSION_COMPAT");
	if (system_version_compat_override != NULL) {
		long override = strtol(system_version_compat_override, NULL, 0);
		if (override == 1) {
			enable_system_version_compat = true;
		} else if (override == 2) {
			enable_ios_version_compat = true;
		}
	} else if (dyld_get_active_platform() == PLATFORM_MACCATALYST) {
		if (!dyld_program_sdk_at_least(dyld_platform_version_iOS_14_0)) {
			enable_system_version_compat = true;
		}
	} else if (dyld_get_active_platform() == PLATFORM_IOS) {
		enable_ios_version_compat = true;
	} else if (!dyld_program_sdk_at_least(dyld_platform_version_macOS_10_16)) {
		enable_system_version_compat = true;
	}

	if (enable_system_version_compat || enable_ios_version_compat) {
		struct _libkernel_late_init_config config = {
			.version = 2,
			.enable_system_version_compat = enable_system_version_compat,
			.enable_ios_version_compat = enable_ios_version_compat,
		};
		__libkernel_init_late(&config);
	}
#endif // TARGET_OS_OSX && !defined(__i386__)

	_libSystem_ktrace0(ARIADNE_LIFECYCLE_libsystem_init | DBG_FUNC_END);

	/* <rdar://problem/11588042>
	 * C99 standard has the following in section 7.5(3):
	 * "The value of errno is zero at program startup, but is never set
	 * to zero by any library function."
	 */
	errno = 0;
}

/*
 * libSystem_atfork_{prepare,parent,child}() are called by libc during fork(2).
 */
void
libSystem_atfork_prepare(void)
{
	// first call client prepare handlers registered with pthread_atfork()
	_pthread_atfork_prepare_handlers();

	// second call hardwired fork prepare handlers for Libsystem components
	// in the _reverse_ order of library initalization above
#if !TARGET_OS_DRIVERKIT
	_libSC_info_fork_prepare();
	xpc_atfork_prepare();
#endif // !TARGET_OS_DRIVERKIT
	dispatch_atfork_prepare();
	_dyld_atfork_prepare();
	cc_atfork_prepare();
	_malloc_fork_prepare();
	_pthread_atfork_prepare();
}

void
libSystem_atfork_parent(void)
{
	// first call hardwired fork parent handlers for Libsystem components
	// in the order of library initalization above
	_pthread_atfork_parent();
	_malloc_fork_parent();
	cc_atfork_parent();
	_dyld_atfork_parent();
	dispatch_atfork_parent();
#if !TARGET_OS_DRIVERKIT
	xpc_atfork_parent();
	_libSC_info_fork_parent();
#endif // !TARGET_OS_DRIVERKIT

#ifdef DARLING
	_mach_fork_parent();
#endif

	// second call client parent handlers registered with pthread_atfork()
	_pthread_atfork_parent_handlers();
}

void
libSystem_atfork_child(void)
{
	// first call hardwired fork child handlers for Libsystem components
	// in the order of library initalization above
	_mach_fork_child();
	_pthread_atfork_child();
	_malloc_fork_child();
	cc_atfork_child();
	_libc_fork_child(); // _arc4_fork_child calls malloc
	_dyld_fork_child();
	dispatch_atfork_child();
#if !TARGET_OS_DRIVERKIT
#if defined(HAVE_SYSTEM_CORESERVICES)
	_libcoreservices_fork_child();
#endif
	_asl_fork_child();
	_notify_fork_child();
	xpc_atfork_child();
#endif // !TARGET_OS_DRIVERKIT
	_libtrace_fork_child();
#if !TARGET_OS_DRIVERKIT
	_libSC_info_fork_child();
#endif // !TARGET_OS_DRIVERKIT

	// second call client parent handlers registered with pthread_atfork()
	_pthread_atfork_child_handlers();

#ifdef DARLING
	kqueue_atfork();
#endif
}

#ifdef DARLING
/*
 * libSystem_posix_spawn_{prepare,parent,child}() are called by libsystem_kernel during posix_spawn(2).
 *
 * These are identical to libSystem_atfork_{prepare,parent,child}(), except they don't call
 * pthread_atfork handlers.
 */
void libSystem_posix_spawn_prepare(void) {
	// call hardwired fork prepare handlers for Libsystem components
	// in the _reverse_ order of library initalization above
#if !TARGET_OS_DRIVERKIT
	_libSC_info_fork_prepare();
	xpc_atfork_prepare();
#endif // !TARGET_OS_DRIVERKIT
	dispatch_atfork_prepare();
	_dyld_atfork_prepare();
	cc_atfork_prepare();
	_malloc_fork_prepare();
	_pthread_atfork_prepare();
};

void libSystem_posix_spawn_parent(void) {
	// call hardwired fork parent handlers for Libsystem components
	// in the order of library initalization above
	_pthread_atfork_parent();
	_malloc_fork_parent();
	cc_atfork_parent();
	_dyld_atfork_parent();
	dispatch_atfork_parent();
#if !TARGET_OS_DRIVERKIT
	xpc_atfork_parent();
	_libSC_info_fork_parent();
#endif // !TARGET_OS_DRIVERKIT

#ifdef DARLING
	_mach_fork_parent();
#endif
};

void libSystem_posix_spawn_child(void) {
	// call hardwired fork child handlers for Libsystem components
	// in the order of library initalization above
	_mach_fork_child();
	_pthread_atfork_child();
	_malloc_fork_child();
	cc_atfork_child();
	_libc_fork_child(); // _arc4_fork_child calls malloc
	_dyld_fork_child();
	dispatch_atfork_child();
#if !TARGET_OS_DRIVERKIT
#if defined(HAVE_SYSTEM_CORESERVICES)
	_libcoreservices_fork_child();
#endif
	_asl_fork_child();
	_notify_fork_child();
	xpc_atfork_child();
#endif // !TARGET_OS_DRIVERKIT
	_libtrace_fork_child();
#if !TARGET_OS_DRIVERKIT
	_libSC_info_fork_child();
#endif // !TARGET_OS_DRIVERKIT

#ifdef DARLING
	kqueue_atfork();
#endif
};
#endif

#if SUPPORT_ASAN

// Prevents use of coloring terminal signals in report. These
// hinder readability when writing to files or the system log.
#define ASAN_OPT_NO_COLOR "color=never"

// Disables ASan's signal handlers. It's better to let the system catch
// these kinds of crashes.
#define ASAN_OPT_NO_SIGNAL_HANDLERS ":handle_segv=0:handle_sigbus=0:handle_sigill=0:handle_sigfpe=0"

// Disables using the out-of-process symbolizer (atos) but still allows
// in-process symbolization via `dladdr()`. This gives useful function names
// (unless they are redacted) which can be helpful in the event we can't
// symbolize offline. Out-of-process symbolization isn't useful because
// the dSYMs are usually not present on the device.
#define ASAN_OPT_NO_OOP_SYMBOLIZER ":external_symbolizer_path="

// Don't try to log to a file. It's difficult to find a location for the file
// that is writable so just write to stderr.
#define ASAN_OPT_FILE_LOG ":log_path=stderr:log_exe_name=0"

// Print the module map when finding an issue. This is necessary for offline
// symbolication.
#define ASAN_OPT_MODULE_MAP ":print_module_map=2"

// Disable ODR violation checking.
// <rdar://problem/71021707> Investigate enabling ODR checking for ASan in BATS and in the `_asan` variant
#define ASAN_OPT_NO_ODR_VIOLATION ":detect_odr_violation=0"

// Start ASan in deactivated mode. This reduces memory overhead until
// instrumented code is loaded. This prevents catching bugs if no instrumented
// code is loaded.
#define ASAN_OPT_START_DEACTIVATED ":start_deactivated=1"

// Do not crash when an error is found. This always works for errors caught via
// ASan's interceptors. This won't work for errors caught in ASan
// instrumentation unless the code is compiled with
// `-fsanitize-recover=address`.  If this option is being used then the ASan
// reports can only be found by looking at the system log.
#define ASAN_OPT_NO_HALT_ON_ERROR ":halt_on_error=0"

// Crash when an error is found.
#define ASAN_OPT_HALT_ON_ERROR ":halt_on_error=1"

// ASan options common to all supported variants
#define COMMON_ASAN_OPTIONS \
  ASAN_OPT_NO_COLOR \
  ASAN_OPT_NO_SIGNAL_HANDLERS \
  ASAN_OPT_NO_OOP_SYMBOLIZER \
  ASAN_OPT_FILE_LOG \
  ASAN_OPT_MODULE_MAP \
  ASAN_OPT_NO_ODR_VIOLATION

#if defined(CURRENT_VARIANT_normal) || defined(CURRENT_VARIANT_debug) || defined (CURRENT_VARIANT_no_asan)

// In the normal variant ASan will be running in all userspace processes ("whole userspace ASan").
// This mode exists to support "ASan in BATS".
//
// Supporting ASan in the debug variant preserves existing behavior.
//
// The no_asan variant does not load the ASan runtime. However, the runtime
// might still be loaded if a program or its dependencies are instrumented.
// There is nothing we can do to prevent this so we should set the appropriate
// ASan options (same as normal variant) if it does happen. We try to do this
// here but this currently doesn't work due to rdar://problem/72212914.
//
// These variants use the following extra options:
//
// ASAN_OPT_NO_HALT_ON_ERROR - Try to avoid crash loops and increase the
//                             chances of booting successfully.
// ASAN_OPT_START_DEACTIVATED - Try to reduce memory overhead.

# define DEFAULT_ASAN_OPTIONS \
  COMMON_ASAN_OPTIONS \
  ASAN_OPT_START_DEACTIVATED \
  ASAN_OPT_NO_HALT_ON_ERROR

#elif defined(CURRENT_VARIANT_asan)

// The `_asan` variant is used to support running proceses with
// `DYLD_IMAGE_SUFFIX=_asan`. This mode is typically used to target select parts of the OS.
//
// It uses the following extra options:
//
// ASAN_OPT_HALT_ON_ERROR - Crashing is better than just writing the error to the system log
//                          if the system can handle this. This workflow is
//                          more tolerant (e.g. `launchctl debug`) to crashing
//                          than the "whole userspace ASan" workflow.

# define DEFAULT_ASAN_OPTIONS \
  COMMON_ASAN_OPTIONS \
  ASAN_OPT_HALT_ON_ERROR

#else
# error Supporting ASan is not supported in the current variant
#endif

char dynamic_asan_opts[1024] = {0};
const char *__asan_default_options(void) {
	int fd = open("/System/Library/Preferences/com.apple.asan.options", O_RDONLY);
	if (fd != -1) {
		ssize_t remaining_size = sizeof(dynamic_asan_opts) - 1;
		char *p = dynamic_asan_opts;
		ssize_t read_bytes = 0;
		do {
			read_bytes = read(fd, p, remaining_size);
			remaining_size -= read_bytes;
		} while (read_bytes > 0);
		close(fd);

		if (dynamic_asan_opts[0]) {
			return dynamic_asan_opts;
		}
	}

	return DEFAULT_ASAN_OPTIONS;
}

#undef ASAN_OPT_NO_COLOR
#undef ASAN_OPT_NO_SIGNAL_HANDLERS
#undef ASAN_OPT_NO_OOP_SYMBOLIZER
#undef ASAN_OPT_FILE_LOG
#undef ASAN_OPT_MODULE_MAP
#undef ASAN_OPT_NO_ODR_VIOLATION
#undef ASAN_OPT_START_DEACTIVATED
#undef ASAN_OPT_NO_HALT_ON_ERROR
#undef ASAN_OPT_HALT_ON_ERROR

#undef COMMON_ASAN_OPTIONS
#undef DEFAULT_ASAN_OPTIONS

#endif

/*
 *  Old crt1.o glue used to call through mach_init_routine which was used to initialize libSystem.
 *  LibSystem now auto-initializes but mach_init_routine is left for binary compatibility.
 */
static void mach_init_old(void) {}
void (*mach_init_routine)(void) = &mach_init_old;

/*
 *	This __crashreporter_info__ symbol is for all non-dylib parts of libSystem.
 */
const char *__crashreporter_info__;
asm (".desc __crashreporter_info__, 0x10");
