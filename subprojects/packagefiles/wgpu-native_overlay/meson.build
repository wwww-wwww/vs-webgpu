project('wgpu-native-prebuilt', 'c')

cc = meson.get_compiler('c')

if host_machine.system() == 'windows'
  deps = [
    cc.find_library('wgpu_native', dirs: meson.current_source_dir() / 'lib'),
    cc.find_library('ws2_32', required: true),
    cc.find_library('ntdll', required: true),
    cc.find_library('userenv', required: true),
  ]
else
  deps = cc.find_library('libwgpu_native', dirs: meson.current_source_dir() / 'lib')
endif

dep = declare_dependency(
  dependencies: deps,
  include_directories: include_directories('include')
)

meson.override_dependency('wgpu-native', dep)
meson.override_dependency('wgpu-native-win', dep)
