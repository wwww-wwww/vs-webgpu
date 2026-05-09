project('wgpu-native-prebuilt', 'c')

cc = meson.get_compiler('c')

dep = declare_dependency(
  dependencies: cc.find_library('libwgpu_native', dirs: meson.current_source_dir() / 'lib'),
  include_directories: include_directories('include')
)

meson.override_dependency('wgpu-native', dep)