add_library(sa_saac STATIC
    SaacClient.cpp
    SaacProtocol.cpp
)

target_include_directories(sa_saac
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sa_saac
    PUBLIC
        sa_idl
    PRIVATE
        sa_platform
)

sa_apply_warnings(sa_saac)
