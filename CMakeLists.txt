#-------------------------------------------------------------------------------
# Project description
#-------------------------------------------------------------------------------
cmake_minimum_required( VERSION 2.8 )
project( XRootDClient )

#set( CMAKE_BUILD_TYPE RelWithDebInfo )
set( CMAKE_BUILD_TYPE Debug )

set( CMAKE_MODULE_PATH
 ${PROJECT_SOURCE_DIR}/cmake )

#-------------------------------------------------------------------------------
# Define the OS variables
#-------------------------------------------------------------------------------
set( Linux    FALSE )
set( MacOSX   FALSE )
set( Solaris  FALSE )
set( OSDEFINE ""    )

if( ${CMAKE_SYSTEM_NAME} STREQUAL "Linux" )
  set( Linux TRUE )
  include( GNUInstallDirs )
  add_definitions( -D__LINUX__=1 )
  add_definitions( -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 )
  set( EXTRA_LIBS rt )
endif()

if( APPLE )
  set( MacOSX TRUE )
  add_definitions( -D__macos__=1 )
  add_definitions( -DLT_MODULE_EXT="dylib" )
  set( CMAKE_INSTALL_LIBDIR "lib" )
  set( CMAKE_INSTALL_BINDIR "bin" )
  set( CMAKE_INSTALL_MANDIR "man" )
endif()

if( ${CMAKE_SYSTEM_NAME} STREQUAL "SunOS" )
  set( CMAKE_INSTALL_LIBDIR "lib" )
  set( CMAKE_INSTALL_BINDIR "bin" )
  set( CMAKE_INSTALL_MANDIR "man" )
  set( Solaris TRUE )
  add_definitions( -D__solaris__=1 )
  add_definitions( -DSUNCC -D_REENTRANT  -D_POSIX_PTHREAD_SEMANTICS )
  set( EXTRA_LIBS rt  Crun Cstd )
endif()

#-------------------------------------------------------------------------------
# Check the system
#-------------------------------------------------------------------------------
find_package( CPPUnit )
find_package( XRootD REQUIRED )
find_package( ZLIB REQUIRED )
find_package( LibEvent )

if( LIBEVENT_FOUND )
  find_package( LibEventPthreads REQUIRED )
  add_definitions( -DHAVE_LIBEVENT )
  set( LIBEVENT_POLLER_FILES XrdClPollerLibEvent.cc XrdClPollerLibEvent.hh )
  set( BUILD_LIBEVENT TRUE )
else()
  set( LIBEVENT_LIB "" )
  set( LIBEVENTPTHREADS_LIB "" )
  set( LIBEVENT_INCLUDE_DIR "" )
  set( LIBEVENT_POLLER_FILES "" )
endif()

ENABLE_TESTING()

find_package( Readline )
if( READLINE_FOUND )
  add_definitions( -DHAVE_READLINE )
else()
  set( READLINE_LIBRARY "" )
  set( NCURSES_LIBRARY "" )
endif()

#-------------------------------------------------------------------------------
# We're using GCC so let's be pedantic
#-------------------------------------------------------------------------------
if( CMAKE_COMPILER_IS_GNUCXX )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wno-unused-parameter" )
endif()

if( CMAKE_COMPILER_IS_GNUCC )
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -Wno-unused-parameter" )
endif()

#-------------------------------------------------------------------------------
# Build in subdirectories
#-------------------------------------------------------------------------------
add_subdirectory( XrdCl )

if( CPPUNIT_FOUND )
  add_subdirectory( XrdClTests )
endif()
