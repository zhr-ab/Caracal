# Gumbo-parser HTML5 parsing library for ESP-IDF
file(GLOB GUMBO_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

if(NOT GUMBO_SRCS)
    message(FATAL_ERROR
        "Gumbo source files not found in ${CMAKE_CURRENT_SOURCE_DIR}/src/\n"
        "Please run fetch_deps.bat from the project root to download them."
    )
endif()

add_compile_options(-Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers)

idf_component_register(SRCS ${GUMBO_SRCS}
                    INCLUDE_DIRS "include"
                    REQUIRES)
