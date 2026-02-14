# cmake/LazyFlatBuffers.cmake

#find_package(FlatBuffers REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Locates the reflection schema provided by the FlatBuffers installation/source
if(NOT FLATBUFFERS_REFLECTION_SCHEMA)
    find_file(FLATBUFFERS_REFLECTION_SCHEMA "reflection/reflection.fbs"
        HINTS 
            "${flatbuffers_SOURCE_DIR}" 
            "${FlatBuffers_SOURCE_DIR}"
            "${FlatBuffers_INCLUDE_DIR}/.."
        PATH_SUFFIXES flatbuffers
        DOC "Path to reflection.fbs, required for generating python bindings"
    )
endif()

function(add_lazy_flatbuffers_library)
    set(options)
    set(oneValueArgs TARGET INCLUDE_PREFIX SCRIPT_PATH GENERATED_INCLUDES_DIR)
    set(multiValueArgs SCHEMAS INCLUDE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "add_lazy_flatbuffers_library: TARGET argument is required.")
    endif()
    
    if(NOT FLATBUFFERS_REFLECTION_SCHEMA)
        message(FATAL_ERROR "add_lazy_flatbuffers_library: Could not find reflection.fbs. Set FLATBUFFERS_REFLECTION_SCHEMA.")
    endif()

    # Default to current binary dir to prevent target-named subdirectories
    if(NOT ARG_GENERATED_INCLUDES_DIR)
        set(ARG_GENERATED_INCLUDES_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    # Prepare include arguments for flatc to handle transitive dependencies
    set(FLATC_INCLUDE_ARGS "")
    foreach(inc ${ARG_INCLUDE})
        list(APPEND FLATC_INCLUDE_ARGS "-I" "${inc}")
    endforeach()

    # 1. Create the Interface Library Target
    if(NOT TARGET ${ARG_TARGET})
        add_library(${ARG_TARGET} INTERFACE)
    endif()
    target_include_directories(${ARG_TARGET} INTERFACE "${ARG_GENERATED_INCLUDES_DIR}")

    # 2. Generate Python Reflection Bindings (Internal Tooling)
    set(REFLECTION_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/python_gen")
    set(REFLECTION_MARKER "${REFLECTION_GEN_DIR}/reflection/Schema.py")

    if(NOT TARGET generate_python_reflection)
        file(MAKE_DIRECTORY "${REFLECTION_GEN_DIR}")
        add_custom_command(
            OUTPUT "${REFLECTION_MARKER}"
            COMMAND flatc --python --no-warnings -o "${REFLECTION_GEN_DIR}" "${FLATBUFFERS_REFLECTION_SCHEMA}"
            DEPENDS "${FLATBUFFERS_REFLECTION_SCHEMA}"
            COMMENT "Generating internal Python reflection library"
            VERBATIM
        )
        add_custom_target(generate_python_reflection DEPENDS "${REFLECTION_MARKER}")
    endif()

    # 3. Process each Schema
    set(ALL_GENERATED_FILES "")
    
    foreach(schema ${ARG_SCHEMAS})
        get_filename_component(schema_name ${schema} NAME_WE)
        get_filename_component(schema_dir ${schema} DIRECTORY)
        
        # Determine output directory and header paths
        if(ARG_INCLUDE_PREFIX)
            set(output_dir "${ARG_GENERATED_INCLUDES_DIR}/${ARG_INCLUDE_PREFIX}")
            set(generated_header "${output_dir}/${schema_name}_generated.h")
            set(traits_header "${output_dir}/${schema_name}_traits.hpp")
            set(rel_include_path "${ARG_INCLUDE_PREFIX}/${schema_name}_generated.h")
        else()
            set(output_dir "${ARG_GENERATED_INCLUDES_DIR}")
            set(generated_header "${output_dir}/${schema_name}_generated.h")
            set(traits_header "${output_dir}/${schema_name}_traits.hpp")
            set(rel_include_path "${schema_name}_generated.h")
        endif()

        file(MAKE_DIRECTORY "${output_dir}")
        set(bfbs_file "${CMAKE_CURRENT_BINARY_DIR}/${schema_name}.bfbs")

        # 3a. Generate C++ Header
        add_custom_command(
            OUTPUT "${generated_header}"
            COMMAND flatc --cpp --no-warnings 
                    -o "${output_dir}"
                    ${FLATC_INCLUDE_ARGS}
                    "${schema}"
            DEPENDS "${schema}"
            COMMENT "Generating FlatBuffer C++ header for ${schema_name}"
            VERBATIM
        )

        # 3b. Generate Binary Schema (.bfbs) with full reflection info
        add_custom_command(
            OUTPUT "${bfbs_file}"
            COMMAND flatc --binary --schema --reflect-names --bfbs-builtins --no-warnings
                    -o "${CMAKE_CURRENT_BINARY_DIR}"
                    ${FLATC_INCLUDE_ARGS}
                    "${schema}"
            DEPENDS "${schema}"
            COMMENT "Generating reflection binary for ${schema_name}"
            VERBATIM
        )

        # 3c. Generate C++ Traits
        # Uses --modify to append to PYTHONPATH (Requires CMake 3.25+)
        add_custom_command(
            OUTPUT "${traits_header}"
            COMMAND ${CMAKE_COMMAND} -E env --modify "PYTHONPATH=path_list_append:${REFLECTION_GEN_DIR}"
                    "${Python3_EXECUTABLE}" "${ARG_SCRIPT_PATH}"
                    "${bfbs_file}" "${traits_header}" "${rel_include_path}"
            DEPENDS "${bfbs_file}" "${ARG_SCRIPT_PATH}" "${REFLECTION_MARKER}"
            COMMENT "Generating C++ traits for ${schema_name}"
            VERBATIM
        )

        list(APPEND ALL_GENERATED_FILES "${generated_header}" "${traits_header}")
    endforeach()

    # Link generated files to the target
    target_sources(${ARG_TARGET} INTERFACE ${ALL_GENERATED_FILES})
    set_source_files_properties(${ALL_GENERATED_FILES} PROPERTIES GENERATED TRUE)

    add_custom_target(${ARG_TARGET}_gen DEPENDS ${ALL_GENERATED_FILES})
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_gen generate_python_reflection)

endfunction()
