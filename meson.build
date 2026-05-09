project('webgpu', 'cpp', 'rust',
  default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20'],
  meson_version: '>=0.64.0',
  version: '1'
)

cxx = meson.get_compiler('cpp')

py = import('python').find_installation(pure: false)
vapoursynth_incl = include_directories(
  run_command(
    py,
    '-c',
    'import vapoursynth as vs; print(vs.get_include())',
    check: true,
  ).stdout().strip()
)

sources = [
  'src/vswebgpu.cpp',
]

wgpu = dependency('wgpu-native', static: true)

libs = []

shared_module('webgpu', sources,
  include_directories: vapoursynth_incl,
  dependencies: [wgpu],
  link_with: libs,
  install: true,
  install_dir: py.get_install_dir() / 'vapoursynth/plugins',
  gnu_symbol_visibility: 'hidden'
)
