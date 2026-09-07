#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SDL3::SDL3-static" for configuration "RelWithDebInfo"
set_property(TARGET SDL3::SDL3-static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(SDL3::SDL3-static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C;CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libSDL3rd.a"
  )

list(APPEND _cmake_import_check_targets SDL3::SDL3-static )
list(APPEND _cmake_import_check_files_for_SDL3::SDL3-static "${_IMPORT_PREFIX}/lib/libSDL3rd.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
