FISSION_EXTERNAL = true
FISSION_LOCATION = "%{wks.location}/Fission"

function include_project(name)
	fission_project(name)
	location(name)
	
	prebuild_shader_compile("%{prj.location}")
	
    files {
		"%{prj.location}/src/**";
		"%{prj.location}/shaders/**"
	}
	
	includedirs {
		'%{wks.location}/include',
		'%{prj.location}/src'
	}
end

workspace 'Voxel-Engine'
	architecture "x86_64"
	configurations { 'Debug', 'Release', 'Dist' }
	
	OutputDir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
	
	flags { 'MultiProcessorCompile', 'MFC' }
	defines '_CRT_SECURE_NO_WARNINGS'
	
	startproject 'Application'
	
	group "Dependencies"
	include 'Fission'
	group ""

	include_project "Application"
