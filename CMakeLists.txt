project(darling-libsystem_init)

cmake_minimum_required(VERSION 3.13)

if(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -nostdlib -Wl,-bind_at_load")

add_definitions(-DHAVE_STDINT_H=1 -DHAVE_SYSTEM_CORESERVICES)

# Hide warnings
add_definitions(
	-w
	-nostdinc
)

set(libsystem_sources
	init.c
	darling/src/dummy.c
	CompatibilityHacks.c
)

set_source_files_properties(init.c PROPERTIES
	COMPILE_FLAGS " -DPRIVATE=1"
)

set(DYLIB_INSTALL_NAME "/usr/lib/libSystem.B.dylib")
set(DYLIB_CURRENT_VERSION "1281.0.0")
add_circular(system FAT
	SOURCES
		${libsystem_sources}
	OBJECTS
		$<TARGET_OBJECTS:kqueue>

	SIBLINGS

	system_malloc
)

function(libsystem_reexport)
	while(ARGN)
		list(GET ARGN 0 sublib)
		list(GET ARGN 1 sublib_path)

		add_dependencies(system ${sublib})
		set_property(TARGET system APPEND_STRING PROPERTY LINK_FLAGS
                " -Wl,-reexport_library -Wl,${CMAKE_BINARY_DIR}/${sublib_path} ")

		list(REMOVE_AT ARGN 0 1)
	endwhile(ARGN)
endfunction(libsystem_reexport)

libsystem_reexport(
	system_sandbox src/sandbox/libsystem_sandbox.dylib
	system_quarantine src/quarantine/libquarantine.dylib
	removefile src/external/removefile/libremovefile.dylib
	system_copyfile src/external/copyfile/libcopyfile.dylib
	system_coreservices src/libsystem_coreservices/libsystem_coreservices.dylib
	system_coretls src/external/coretls/libsystem_coretls.dylib
	system_malloc src/external/libmalloc/libsystem_malloc.dylib
	system_c src/external/libc/libsystem_c.dylib
	system_kernel src/external/xnu/darling/src/libsystem_kernel/libsystem_kernel.dylib
	system_trace src/external/libtrace/libsystem_trace.dylib
	keymgr src/external/keymgr/libkeymgr.dylib
	system_m src/libm/libsystem_m.dylib
	system_info src/external/Libinfo/libsystem_info.dylib
	system_notify src/external/libnotify/libsystem_notify.dylib
	libdispatch_shared src/external/libdispatch/libdispatch.dylib
	launch src/launchd/liblaunch/liblaunch.dylib
	compiler_rt src/external/compiler-rt/lib/builtins/libcompiler_rt.dylib
	system_dyld src/external/dyld/libdyld.dylib
	macho src/external/cctools/libmacho/libmacho.dylib
	system_pthread src/external/libpthread/src/libsystem_pthread.dylib
	platform src/external/libplatform/libsystem_platform.dylib
	system_asl src/external/syslog/libsystem_asl.tproj/libsystem_asl.dylib
	system_duct src/duct/src/libsystem_duct.dylib
	system_blocks src/external/libclosure/libsystem_blocks.dylib
	xpc src/external/libxpc/libxpc.dylib
	corecrypto src/external/corecrypto/libcorecrypto.dylib
	commonCrypto src/external/commoncrypto/libcommonCrypto.dylib
	system_dnssd src/lib/system/dnssd/libsystem_dnssd.dylib
	system_networkextension src/networkextension/libsystem_networkextension.dylib
	unwind src/external/libunwind/libunwind.dylib
	system_darwin src/external/libc/libdarwin/libsystem_darwin.dylib
	libcache src/libcache/libcache.dylib
	system_configuration src/external/configd/libsystem_configuration.dylib
)

add_dependencies(system system_malloc system_m)

set_target_properties(system PROPERTIES OUTPUT_NAME "System.B")

make_fat(system)

install(TARGETS system DESTINATION libexec/darling/usr/lib)

InstallSymlink("libSystem.B.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libSystem.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libc.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libdbm.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libdl.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libinfo.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libdl.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libm.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libmx.A.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libmx.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libpoll.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libproc.dylib")
InstallSymlink("libSystem.dylib" "${CMAKE_INSTALL_PREFIX}/libexec/darling/usr/lib/libpthread.dylib")
