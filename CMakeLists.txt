cmake_minimum_required(VERSION 3.8)

set(VERSION 0.0.0)
project(amtail C)
project(amtail VERSION ${VERSION})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

#include(${CMAKE_BINARY_DIR}/cmakedeps_macros.cmake)
#conan_basic_setup()

set(CMAKE_BUILD_TYPE Debug)
set(HEAD ${PROJECT_NAME})

# for speed check add -H -ftime-report
set(CMAKE_C_FLAGS "-Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wno-undef -Wunreachable-code -Wno-unreachable-code -Wno-unused-variable -O3 -std=gnu11 -pthread -pg -lbsd")
if(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk")
endif()

# Some environments leak -lbsd into CFLAGS; it must not be passed during compile.
foreach(_flag_var
        CMAKE_C_FLAGS
        CMAKE_C_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL)
    if(DEFINED ${_flag_var})
        string(REPLACE "-lbsd" "" ${_flag_var} "${${_flag_var}}")
    endif()
endforeach()

if(UNIX AND NOT APPLE)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Linux clang may see linker-only args from environment (e.g. -lbsd) during compile.
        add_compile_options(-Qunused-arguments -Wno-unused-command-line-argument -Wno-error=unused-command-line-argument)
    endif()
endif()
include_directories(/usr/local/include)
if(APPLE)
    include_directories(/opt/homebrew/Cellar/jansson/2.14.1/include/)
    include_directories(/System/Volumes/Data/opt/homebrew/Cellar/pcre/8.45/include/)
endif()
include_directories(../alligator/src)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

link_directories(/usr/local/lib/)

set(AMTAIL_CORE_SOURCES
    generator.c
    file.c
    selector.c
    ../alligator/src/dstructures/tommy.c
    ../alligator/src/dstructures/tommyds/tommyds/tommyhash.c
    ../alligator/src/dstructures/tommyds/tommyds/tommyhashdyn.c
    ../alligator/src/dstructures/ht.c
    compile.c
    lex.c
    parser.c
    vm.c
    variables.c
    amtail_pcre.c
)

add_library(${HEAD}_static STATIC ${AMTAIL_CORE_SOURCES})
set_target_properties(${HEAD}_static PROPERTIES
    C_STANDARD 11
    OUTPUT_NAME ${HEAD}
)
target_include_directories(${HEAD}_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../alligator/src
)

add_executable(${HEAD} test.c)
set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)

find_library(PCRE_LIB NAMES pcre HINTS /usr/lib64 /usr/lib /usr/local/lib)
if(NOT PCRE_LIB)
    message(FATAL_ERROR "PCRE library not found. Install pcre-devel (or equivalent).")
endif()

set(EXTRA_PLATFORM_LIBS "")
if(UNIX AND NOT APPLE)
    find_library(BSD_LIB NAMES bsd HINTS /usr/lib64 /usr/lib /usr/local/lib)
    if(BSD_LIB)
        list(APPEND EXTRA_PLATFORM_LIBS ${BSD_LIB})
    endif()
endif()

target_link_libraries(${HEAD} ${HEAD}_static ${PCRE_LIB} pthread m ${EXTRA_PLATFORM_LIBS})

install(TARGETS ${HEAD}_static
    ARCHIVE DESTINATION lib
)

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_FREEBSD_PACKAGE_LICENSE "APACHE20")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "amoshi.mandrakeuser@gmail.com")
set(CPACK_PACKAGE_CONTACT "amoshi.mandrakeuser@gmail.com")

install(FILES alligator.service DESTINATION /usr/lib/systemd/system)
install(FILES /var/lib/alligator/parseJks.so DESTINATION /var/lib/alligator)
install(FILES /var/lib/alligator/mongo.so DESTINATION /var/lib/alligator)
install(FILES /var/lib/alligator/puppeteer-alligator.js DESTINATION /var/lib/alligator)
install(FILES bin/alligator
	PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
	DESTINATION /usr/bin)

install(DIRECTORY DESTINATION /var/lib/alligator/)

set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION	/usr/bin
							/usr/lib/systemd
							/usr/lib/systemd/system)

include(CPack)
