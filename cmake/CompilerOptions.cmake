# Shared compiler policy; each library still declares its own source/dependencies.
function(serverengine_target_defaults target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_17)
    set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE
            UNICODE _UNICODE _CONSOLE WIN32_LEAN_AND_MEAN NOMINMAX
            $<$<EQUAL:${CMAKE_SIZEOF_VOID_P},4>:WIN32>
        )
        target_compile_options(${target_name} PRIVATE
            /W3 /permissive- /sdl
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/GL>
            $<$<CONFIG:Release>:/Oi>
        )
    endif()
endfunction()
