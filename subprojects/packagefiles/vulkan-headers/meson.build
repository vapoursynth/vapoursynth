# Header only fallback for machines without a Vulkan SDK or distro headers package. The
# headers are a build requirement on every platform (see the main meson.build), but nothing
# links against the loader, so this is purely include paths. Pinned rather than tracking
# latest so CI and development compile against the same declarations.
project('vulkan-headers',
    version: '1.4.357',
    license: 'Apache-2.0 OR MIT',
    meson_version: '>=0.63.0',
)

vulkan_headers_dep = declare_dependency(
    include_directories: include_directories('include'),
)

meson.override_dependency('vulkan-headers', vulkan_headers_dep)
