# system capabilities checking

# initial system defaults
if(CMAKE_COMPILER_IS_GNUCC)
  set(MRUBY_LIBS m)
else()
  if(MSVC)
    # TODO default MSVC flags
    add_definitions(
      -D_CRT_SECURE_NO_WARNINGS
      -wd4018  # suppress 'signed/unsigned mismatch'
      )
  endif()
endif()

#add_definitions(-DDISABLE_GEMS)

if(MSVC)
  add_definitions(
    -DRUBY_EXPORT   # required by oniguruma.h
    )
endif()


# include helpers
include(CheckIncludeFile)
include(CheckSymbolExists)

# header checks
CHECK_INCLUDE_FILE(string.h HAVE_STRING_H)
if(HAVE_STRING_H)
  add_definitions(-DHAVE_STRING_H)
endif()

CHECK_INCLUDE_FILE(float.h HAVE_FLOAT_H)
if(HAVE_FLOAT_H)
  add_definitions(-DHAVE_FLOAT_H)
endif()


# symbol checks
CHECK_SYMBOL_EXISTS(gettimeofday sys/time.h HAVE_GETTIMEOFDAY)
if(NOT HAVE_GETTIMEOFDAY)
  add_definitions(-DNO_GETTIMEOFDAY)
endif()

