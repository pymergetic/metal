# Vendored net stack: mbedTLS + nghttp2 + libcurl (HTTP/1.1 + HTTP/2).
# Called from src/linux/CMakeLists.txt. See scripts/setup-net.sh.
#
# Monocypher is compiled as a single source next to util/crypto.c (lz4 shape),
# not via this file.

set(PM_MBEDTLS_DIR "${PM_METAL_ROOT}/external/mbedtls")
set(PM_NGHTTP2_DIR "${PM_METAL_ROOT}/external/nghttp2")
set(PM_CURL_DIR "${PM_METAL_ROOT}/external/curl")

foreach(_pm_net_dir PM_MBEDTLS_DIR PM_NGHTTP2_DIR PM_CURL_DIR)
	if (NOT EXISTS "${${_pm_net_dir}}/CMakeLists.txt")
		message(FATAL_ERROR
			"${${_pm_net_dir}} missing — run scripts/setup-net.sh")
	endif ()
endforeach()

# ---- mbedTLS ----
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${PM_MBEDTLS_DIR} ${CMAKE_BINARY_DIR}/mbedtls EXCLUDE_FROM_ALL)

# ---- nghttp2 (lib only, static) ----
set(ENABLE_LIB_ONLY ON CACHE BOOL "" FORCE)
set(ENABLE_STATIC_LIB ON CACHE BOOL "" FORCE)
set(ENABLE_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_FAILALLOC OFF CACHE BOOL "" FORCE)
add_subdirectory(${PM_NGHTTP2_DIR} ${CMAKE_BINARY_DIR}/nghttp2 EXCLUDE_FROM_ALL)

# ---- libcurl (HTTP-only, mbedTLS, HTTP/2) ----
# Point curl's Find* modules at the targets we just added.
set(MBEDTLS_INCLUDE_DIRS "${PM_MBEDTLS_DIR}/include" CACHE PATH "" FORCE)
set(MBEDTLS_INCLUDE_DIR "${PM_MBEDTLS_DIR}/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARY mbedtls CACHE FILEPATH "" FORCE)
set(MBEDX509_LIBRARY mbedx509 CACHE FILEPATH "" FORCE)
set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE FILEPATH "" FORCE)
set(MBEDTLS_LIBRARIES "mbedtls;mbedx509;mbedcrypto" CACHE STRING "" FORCE)
set(MBEDTLS_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

set(NGHTTP2_INCLUDE_DIRS "${PM_NGHTTP2_DIR}/lib/includes" CACHE PATH "" FORCE)
set(NGHTTP2_INCLUDE_DIR "${PM_NGHTTP2_DIR}/lib/includes" CACHE PATH "" FORCE)
# nghttp2 static target name varies by version; prefer nghttp2_static.
if (TARGET nghttp2_static)
	set(NGHTTP2_LIBRARY nghttp2_static CACHE FILEPATH "" FORCE)
else ()
	set(NGHTTP2_LIBRARY nghttp2 CACHE FILEPATH "" FORCE)
endif ()
set(NGHTTP2_LIBRARIES "${NGHTTP2_LIBRARY}" CACHE STRING "" FORCE)
set(NGHTTP2_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(USE_NGHTTP2 ON CACHE BOOL "" FORCE)
set(ENABLE_THREADED_RESOLVER ON CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)

add_subdirectory(${PM_CURL_DIR} ${CMAKE_BINARY_DIR}/curl EXCLUDE_FROM_ALL)

# Linked by pm-linux-runtime (this file is include()'d, not a subdir).
set(PM_METAL_NET_LIBS libcurl mbedtls mbedx509 mbedcrypto)
if (TARGET nghttp2_static)
	list(APPEND PM_METAL_NET_LIBS nghttp2_static)
elseif (TARGET nghttp2)
	list(APPEND PM_METAL_NET_LIBS nghttp2)
endif ()
