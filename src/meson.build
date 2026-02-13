# SPDX-License-Identifier: GPL-2.0-only 
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#


project('pitrac_lm', 'c', 'cpp',
    meson_version : '>= 0.61',
    version : '0.1.0',
    default_options : [
        'werror=true',
        'warning_level=2',
        'cpp_std=c++20',
    ],
    license : 'LGPL 2.1+')

# Warning suppressions - these should be addressed over time
# TODO: Fix underlying issues and remove these suppressions
add_global_arguments('-Wno-deprecated-enum-enum-conversion', language : 'cpp')
add_global_arguments('-Wno-deprecated-declarations', language : 'cpp')
add_global_arguments('-Wno-comment', language : 'cpp')
add_global_arguments('-Wno-unused', language : 'cpp')
add_global_arguments('-Wno-strict-aliasing', language : 'cpp')  # Type-punning in SetConstant calls

# Required definitions
add_global_arguments('-D_FILE_OFFSET_BITS=64', language : 'cpp')  # Support for large files
add_global_arguments('-DLIBCXX_ENABLE_INCOMPLETE_FEATURES=ON', language : 'cpp')

# Boost configuration
add_global_arguments('-DBOOST_LOG_DYN_LINK', language : 'cpp')
add_global_arguments('-DBOOST_BIND_GLOBAL_PLACEHOLDERS', language : 'cpp')

# ONNX Runtime configuration
add_global_arguments('-DUSE_XNNPACK', language : 'cpp')  # Enable XNNPACK execution provider


fs = import('fs')
python = import('python')
py3 = python.find_installation()

# We need a relatively recent version of gcc that will support c++20

cxx = meson.get_compiler('cpp')
cpu = host_machine.cpu()

cpp_arguments = []

if cxx.get_id() == 'gcc'
    cpp_arguments += '-Wno-psabi'
endif

if cpu == 'aarch64' or neon == 'arm64'
    cpp_arguments += [
        '-mcpu=cortex-a76',
        '-ftree-vectorize',
        '-falign-functions=16',
        '-falign-loops=16',
        '-fomit-frame-pointer',
        '-ffast-math',  # Includes funsafe-math-optimizations, fno-signed-zeros, fno-trapping-math
        # XNNPACK-specific optimizations
        '-funroll-loops',           # Better for XNNPACK's compute kernels
        '-fprefetch-loop-arrays',   # Improve cache utilization
        '-falign-jumps=16',         # Better branch prediction
        '-moutline-atomics'         # Better for Pi4/5 atomic operations
    ]
elif neon == 'armv8-neon'
    cpp_arguments += ['-mfpu=neon-fp-armv8', '-ftree-vectorize']
endif

ssl_dep = dependency('openssl', required : true)
activemq_dep = dependency('activemq-cpp', required : true)
apr_dep = cxx.find_library('apr-1', required : true)

libcamera_dep = dependency('libcamera', required : true)
thread_dep = dependency('threads', required : true)
opencv_dep = dependency('opencv4', required : true, version : '>=4.9.0', include_type: 'system',)

lgpio_dep = dependency('lgpio', required : true)
fmt_dep = dependency('fmt', required : true)

boost_dep = dependency('boost', method: 'pkg-config', version : '>=1.74.0', modules : ['timer', 'log', 'thread', 'filesystem', 'regex'])

msgpack_dep = dependency('msgpack-cxx', required : true)
yamlcpp_dep = dependency('yaml-cpp', required : true)
onnxruntime_dep = dependency('onnxruntime', required : true)

rpicam_app_src = []
rpicam_app_dep = [libcamera_dep, lgpio_dep]


pitrac_lm_module_deps = [
	libcamera_dep, thread_dep, opencv_dep, lgpio_dep, rpicam_app_dep,
	fmt_dep, boost_dep, activemq_dep, ssl_dep, apr_dep, msgpack_dep, yamlcpp_dep, onnxruntime_dep,]

# Include directories used by all modules
# Note: include_directories() creates paths relative to the current meson.build
pitrac_lm_include_dirs = include_directories('.')

subdir('utils')
subdir('core')
subdir('encoder')
subdir('image')
subdir('output')
subdir('preview')
subdir('post_processing_stages')


# Generate a version string.
pitrac_base_version = get_option('pitrac_version')
if pitrac_base_version == ''
    pitrac_base_version = meson.project_version()
endif
version_cmd = [meson.project_source_root() / 'utils' / 'version.py', pitrac_base_version]

# Check if a version.gen file is present.
# This would have been generated from the meson dist command.
dist_version_file = meson.project_source_root() / 'version.gen'
if fs.is_file(dist_version_file)
    version_cmd += fs.read(dist_version_file)
endif

version_cpp = vcs_tag(command : version_cmd,
                      replace_string: '@VER@',
                      input : meson.project_source_root() / 'core' / 'version.cpp.in',
                      output : 'version.cpp',
                      fallback : meson.project_version())

rpicam_app_src += version_cpp


enable_recompile_closed_source = get_option('enable_recompile_closed_source')
enable_compile_on_pi4 = get_option('enable_compile_on_pi4')

# TBD - This is mostly deprecated.  Delete soon!
if enable_compile_on_pi4
  message('Compiling on a Pi 4')
  add_global_arguments('-DPITRAC_COMPILING_ON_PI_4', language : 'cpp')
else
  message('Compiling on a Pi 5')
endif

pitrac_lm_sources = []

libav_dep_names = ['libavcodec', 'libavdevice', 'libavformat', 'libavutil', 'libswresample']
libav_deps = []

pitrac_lm_module_deps += ([
		rpicam_app_dep,
])

sim_sources = [
    'sim/common/gs_sim_interface.cpp',
    'sim/common/gs_sim_socket_interface.cpp',
    'sim/gspro/gs_gspro_interface.cpp',
    'sim/gspro/gs_gspro_response.cpp',
    'sim/gspro/gs_gspro_results.cpp',
    'sim/gspro/gs_gspro_test_server.cpp',
]

vision_sources = [
    'ED.cpp',
    'EDColor.cpp',
    'EDPF.cpp',
    'EllipseDetectorCommon.cpp',
    'EllipseDetectorYaed.cpp',
    'ball_watcher.cpp',
    'ball_watcher_image_buffer.cpp',
    'ball_image_proc.cpp',
    'onnx_runtime_detector.cpp',
    'colorsys.cpp',
    'golf_ball.cpp',
]

core_sources = [
    'gs_globals.cpp',
    'gs_fsm.cpp',
    'libcamera_interface.cpp',
    'libcamera_jpeg.cpp',
    'gs_automated_testing.cpp',
    'gs_calibration.cpp',
    'gs_camera.cpp',
    'gs_web_api.cpp',
    'gs_clubs.cpp',
    'gs_club_data.cpp',
    'gs_options.cpp',
    'gs_config.cpp',
    'configuration_manager.cpp',
    'gs_events.cpp',
    'worker_thread.cpp',
    'camera_hardware.cpp',
    'gs_ipc_message.cpp',
    'gs_ipc_control_msg.cpp',
    'gs_results.cpp',
    'gs_ui_system.cpp',
    'gs_ipc_mat.cpp',
    'gs_ipc_result.cpp',
    'gs_ipc_test.cpp',
    'gs_ipc_system.cpp',
    'gs_message_consumer.cpp',
    'gs_message_producer.cpp',
    'pulse_strobe.cpp',
]

core_sources += rpicam_app_src

sim_lib = static_library('sim',
    sources: sim_sources,
    include_directories : pitrac_lm_include_dirs,
    dependencies : pitrac_lm_module_deps)

vision_lib = static_library('vision',
    vision_sources,
    include_directories : pitrac_lm_include_dirs,
    dependencies : pitrac_lm_module_deps)

core_lib = static_library('core',
    core_sources,
    include_directories : pitrac_lm_include_dirs,
    dependencies : pitrac_lm_module_deps)

# Now that libraries are defined, we can build tests that link against them
subdir('tests')

pitrac_lm_sources += ['lm_main.cpp']

exec = executable('pitrac_lm',
	pitrac_lm_sources,
	include_directories : pitrac_lm_include_dirs,
	install : true,
        link_with : [core_lib, vision_lib, sim_lib, utils_lib],
	dependencies : pitrac_lm_module_deps
	)
