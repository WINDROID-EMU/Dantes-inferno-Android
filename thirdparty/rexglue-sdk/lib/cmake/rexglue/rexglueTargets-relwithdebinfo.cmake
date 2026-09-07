#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rex::runtime" for configuration "RelWithDebInfo"
set_property(TARGET rex::runtime APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::runtime PROPERTIES
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/librexruntimerd.so"
  IMPORTED_SONAME_RELWITHDEBINFO "librexruntimerd.so"
  )

list(APPEND _cmake_import_check_targets rex::runtime )
list(APPEND _cmake_import_check_files_for_rex::runtime "${_IMPORT_PREFIX}/lib/librexruntimerd.so" )

# Import target "rex::gpu-xenos" for configuration "RelWithDebInfo"
set_property(TARGET rex::gpu-xenos APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::gpu-xenos PROPERTIES
  IMPORTED_LINK_DEPENDENT_LIBRARIES_RELWITHDEBINFO "rex::runtime"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/librexgpu-xenosrd.so"
  IMPORTED_SONAME_RELWITHDEBINFO "librexgpu-xenosrd.so"
  )

list(APPEND _cmake_import_check_targets rex::gpu-xenos )
list(APPEND _cmake_import_check_files_for_rex::gpu-xenos "${_IMPORT_PREFIX}/lib/librexgpu-xenosrd.so" )

# Import target "rex::aes128" for configuration "RelWithDebInfo"
set_property(TARGET rex::aes128 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::aes128 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libaes128rd.a"
  )

list(APPEND _cmake_import_check_targets rex::aes128 )
list(APPEND _cmake_import_check_files_for_rex::aes128 "${_IMPORT_PREFIX}/lib/libaes128rd.a" )

# Import target "rex::mspack" for configuration "RelWithDebInfo"
set_property(TARGET rex::mspack APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::mspack PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libmspackrd.a"
  )

list(APPEND _cmake_import_check_targets rex::mspack )
list(APPEND _cmake_import_check_files_for_rex::mspack "${_IMPORT_PREFIX}/lib/libmspackrd.a" )

# Import target "rex::o1heap" for configuration "RelWithDebInfo"
set_property(TARGET rex::o1heap APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::o1heap PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libo1heaprd.a"
  )

list(APPEND _cmake_import_check_targets rex::o1heap )
list(APPEND _cmake_import_check_files_for_rex::o1heap "${_IMPORT_PREFIX}/lib/libo1heaprd.a" )

# Import target "rex::disasm" for configuration "RelWithDebInfo"
set_property(TARGET rex::disasm APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::disasm PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libdisasmrd.a"
  )

list(APPEND _cmake_import_check_targets rex::disasm )
list(APPEND _cmake_import_check_files_for_rex::disasm "${_IMPORT_PREFIX}/lib/libdisasmrd.a" )

# Import target "rex::xxhash" for configuration "RelWithDebInfo"
set_property(TARGET rex::xxhash APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::xxhash PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libxxhashrd.a"
  )

list(APPEND _cmake_import_check_targets rex::xxhash )
list(APPEND _cmake_import_check_files_for_rex::xxhash "${_IMPORT_PREFIX}/lib/libxxhashrd.a" )

# Import target "rex::libavcodec" for configuration "RelWithDebInfo"
set_property(TARGET rex::libavcodec APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::libavcodec PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "ASM;C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/liblibavcodecrd.a"
  )

list(APPEND _cmake_import_check_targets rex::libavcodec )
list(APPEND _cmake_import_check_files_for_rex::libavcodec "${_IMPORT_PREFIX}/lib/liblibavcodecrd.a" )

# Import target "rex::libavutil" for configuration "RelWithDebInfo"
set_property(TARGET rex::libavutil APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::libavutil PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "ASM;C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/liblibavutilrd.a"
  )

list(APPEND _cmake_import_check_targets rex::libavutil )
list(APPEND _cmake_import_check_files_for_rex::libavutil "${_IMPORT_PREFIX}/lib/liblibavutilrd.a" )

# Import target "rex::SPIRV" for configuration "RelWithDebInfo"
set_property(TARGET rex::SPIRV APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::SPIRV PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libSPIRVrd.a"
  )

list(APPEND _cmake_import_check_targets rex::SPIRV )
list(APPEND _cmake_import_check_files_for_rex::SPIRV "${_IMPORT_PREFIX}/lib/libSPIRVrd.a" )

# Import target "rex::glslang" for configuration "RelWithDebInfo"
set_property(TARGET rex::glslang APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::glslang PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libglslangrd.a"
  )

list(APPEND _cmake_import_check_targets rex::glslang )
list(APPEND _cmake_import_check_files_for_rex::glslang "${_IMPORT_PREFIX}/lib/libglslangrd.a" )

# Import target "rex::MachineIndependent" for configuration "RelWithDebInfo"
set_property(TARGET rex::MachineIndependent APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::MachineIndependent PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libMachineIndependentrd.a"
  )

list(APPEND _cmake_import_check_targets rex::MachineIndependent )
list(APPEND _cmake_import_check_files_for_rex::MachineIndependent "${_IMPORT_PREFIX}/lib/libMachineIndependentrd.a" )

# Import target "rex::GenericCodeGen" for configuration "RelWithDebInfo"
set_property(TARGET rex::GenericCodeGen APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::GenericCodeGen PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libGenericCodeGenrd.a"
  )

list(APPEND _cmake_import_check_targets rex::GenericCodeGen )
list(APPEND _cmake_import_check_files_for_rex::GenericCodeGen "${_IMPORT_PREFIX}/lib/libGenericCodeGenrd.a" )

# Import target "rex::OSDependent" for configuration "RelWithDebInfo"
set_property(TARGET rex::OSDependent APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::OSDependent PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libOSDependentrd.a"
  )

list(APPEND _cmake_import_check_targets rex::OSDependent )
list(APPEND _cmake_import_check_files_for_rex::OSDependent "${_IMPORT_PREFIX}/lib/libOSDependentrd.a" )

# Import target "rex::OGLCompiler" for configuration "RelWithDebInfo"
set_property(TARGET rex::OGLCompiler APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(rex::OGLCompiler PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libOGLCompilerrd.a"
  )

list(APPEND _cmake_import_check_targets rex::OGLCompiler )
list(APPEND _cmake_import_check_files_for_rex::OGLCompiler "${_IMPORT_PREFIX}/lib/libOGLCompilerrd.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
