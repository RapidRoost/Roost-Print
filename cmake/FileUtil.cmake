include(BuildInfoUtil)

macro(__source_recurse dir src)
	file(GLOB_RECURSE _tmp_list ${dir}/*.h ${dir}/*.hpp ${dir}/*.cpp ${dir}/*.c ${dir}/*.inl)
	set(${src} ${_tmp_list})
	#message("${${src}}")
endmacro()
macro(__files_group dir src)   #support 2 level
	file(GLOB _src ${dir}/*.h ${dir}/*.cpp ${dir}/*.cc ${dir}/*.c ${dir}/*.proto)
	file(GLOB children RELATIVE ${dir} ${dir}/*)
	foreach(child ${children})
		set(sub_dir ${dir}/${child})
		if(IS_DIRECTORY ${sub_dir})
			file(GLOB sub_src ${sub_dir}/*.h ${sub_dir}/*.cpp ${sub_dir}/*.cc ${sub_dir}/*.c)
			source_group(${child} FILES ${sub_src})
			set(_src ${_src} ${sub_src})
		endif()
	endforeach()
	set(${src} ${_src})
endmacro()

macro(__files_group_2 dir folder src)   #support 2 level
	file(GLOB _src ${dir}/*.h ${dir}/*.cpp)
	file(GLOB children RELATIVE ${dir} ${dir}/*)
	foreach(child ${children})
		set(sub_dir ${dir}/${child})
		if(IS_DIRECTORY ${sub_dir})
			file(GLOB sub_src ${sub_dir}/*.h ${sub_dir}/*.cpp)
			source_group(${folder}/${child} FILES ${sub_src})
			set(_src ${_src} ${sub_src})
		endif()
	endforeach()
	set(${src} ${_src})
endmacro()

macro(__files_group_c dir src)   #support 2 level
	file(GLOB _src ${dir}/*.c)
	file(GLOB children RELATIVE ${dir} ${dir}/*)
	foreach(child ${children})
		set(sub_dir ${dir}/${child})
		if(IS_DIRECTORY ${sub_dir})
			file(GLOB sub_src ${sub_dir}/*.c)
			source_group(${child} FILES ${sub_src})
			set(_src ${_src} ${sub_src})
		endif()
	endforeach()
	set(${src} ${_src})
endmacro()

function(__recursive_add_subdirectory dir)
	file(GLOB children RELATIVE ${dir} ${dir}/*)
	foreach(child ${children})
		set(sub_dir ${dir}/${child})
		if(IS_DIRECTORY ${sub_dir} AND EXISTS ${sub_dir}/CMakeLists.txt)
			add_subdirectory(${sub_dir})
		endif()
	endforeach()
endfunction()
macro(__copy_files copy_target files)
	add_custom_target(${copy_target} ALL COMMENT "copy third party dll!")
	__set_target_folder(${copy_target} CMakePredefinedTargets)

	add_custom_command(TARGET ${copy_target} PRE_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${BIN_OUTPUT_DIR}/$<$<CONFIG:Debug>:Debug>$<$<CONFIG:Release>:Release>"
		)
	foreach(file ${${files}})
		add_custom_command(TARGET ${copy_target} PRE_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different  
				"$<$<CONFIG:Release>:${file}>"  
				"$<$<CONFIG:Debug>:${file}>" 
				"${BIN_OUTPUT_DIR}/$<$<CONFIG:Debug>:Debug>$<$<CONFIG:Release>:Release>"
			)	
	endforeach()
endmacro()
