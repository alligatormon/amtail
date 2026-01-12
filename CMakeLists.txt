cmake_minimum_required(VERSION 3.8)

set(VERSION 0.0.0)
project(amtail C)
project(amtail VERSION ${VERSION})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/cmakedeps_macros.cmake)
#conan_basic_setup()

set(CMAKE_BUILD_TYPE Debug)
set(HEAD ${PROJECT_NAME})

# for speed check add -H -ftime-report
set(CMAKE_C_FLAGS "-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -Werror -Wall -Wno-implicit-function-declaration -Wno-char-subscripts -Wno-error=format-extra-args -Wno-error=format -Wno-variadic-macros -Wno-error=unused-result -Wno-undef -Wunreachable-code -Wno-unreachable-code -O3 -std=gnu11 -pthread -pg")
include_directories(/usr/local/include)
include_directories(/opt/homebrew/Cellar/jansson/2.14.1/include/)
include_directories(../alligator/src)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

link_directories(/usr/local/lib/)

#set(STATIC_FILES
#    generator.c
#    file.c
#    selector.c
#    ../../src/dstructures/tommy.c
#    ../../src/dstructures/tommyds/tommyds/tommyhash.c
#    ../../src/dstructures/tommyds/tommyds/tommyhashdyn.c
#    ../../src/dstructures/ht.c
#    compile.c
#    lex.c
#    parser.c
#    vm.c
#    variables.c
#)
#add_library(${COMMON} STATIC ${STATIC_FILES} ${RPM_FILES})
add_executable(${HEAD} test.c
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

set_target_properties(${HEAD} PROPERTIES C_STANDARD 11)

target_link_libraries(${HEAD} ${COMMON} libpcre.a pthread)

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
