# function: detect os id
# result: OUTPUT_VAR
# usage eg: detect_os(OS_ID)   OS_ID may is uos/deepin/Ubuntu

function(detect_os OUTPUT_VAR)
    if(UNIX AND NOT APPLE)
        if(EXISTS /etc/os-release)
            file(STRINGS /etc/os-release os_release_lines)
            set(os_id "")
            foreach(line IN LISTS os_release_lines)
                if(line MATCHES "^ID=(.*)$")
                    set(os_id "${CMAKE_MATCH_1}")
                endif()
            endforeach()
            set(${OUTPUT_VAR} "${os_id}" PARENT_SCOPE)
        else()
            set(${OUTPUT_VAR} "unknown" PARENT_SCOPE)
        endif()
    else()
        set(${OUTPUT_VAR} "non-unix" PARENT_SCOPE)
    endif()
endfunction()
