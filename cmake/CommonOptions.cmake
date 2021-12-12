if (NOT DEFINED TREMOTESF_QT6)
    message(FATAL_ERROR "TREMOTESF_QT6 is not defined")
endif()

if (TREMOTESF_QT6)
    set(TREMOTESF_QT_VERSION_MAJOR 6)
else()
    set(TREMOTESF_QT_VERSION_MAJOR 5)
endif()

if (MSVC)
    set(extra_warnings /W4)
else()
    set(
        common_compile_options
        -Wall
        -Wextra
        -Wpedantic
        -Wnon-virtual-dtor
        -Wcast-align
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wformat=2
        -Werror=format
    )
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND common_compile_options -Wlogical-op)
    endif()
endif()

set(
    common_compile_definitions
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_BEFORE=0x050e00
)

set(common_public_compile_features cxx_std_17)

if (NOT DEFINED TREMOTESF_SAILFISHOS)
    message(FATAL_ERROR "TREMOTESF_SAILFISHOS is not defined")
endif()

if (NOT TREMOTESF_SAILFISHOS)
    list(APPEND common_compile_definitions QT_MESSAGELOGCONTEXT)
endif()

set(common_properties CXX_EXTENSIONS OFF)

function(set_common_options_on_targets)
    get_directory_property(targets BUILDSYSTEM_TARGETS)
    foreach (target ${targets})
        get_target_property(type ${target} TYPE)
        if ((TYPE STREQUAL EXECUTABLE) OR (TYPE STREQUAL SHARED_LIBRARY) OR (TYPE STREQUAL STATIC_LIBRARY) OR (TYPE STREQUAL OBJECT_LIBRARY))
            target_compile_options(${target} PRIVATE ${common_compile_options})
            target_compile_definitions(${target} PRIVATE ${common_compile_definitions})
            target_compile_features(${target} PUBLIC ${common_public_compile_features})
            set_target_properties(${target} PROPERTIES ${common_properties})
        endif()
    endforeach()
endfunction()
