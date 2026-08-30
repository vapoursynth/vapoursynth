project('vulkan-headers',
    license: 'Apache-2.0 OR MIT',
    version: '1.4.357',
)

vulkan_headers_dep = declare_dependency(
    include_directories: include_directories('include', is_system: true),
)

meson.override_dependency('vulkan-headers', vulkan_headers_dep)
