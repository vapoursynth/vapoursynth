# build_info.h must sit under a glslang/ directory in the build tree because the
# sources include it as "glslang/build_info.h"; configure_file cannot place output in
# a subdirectory, hence this one line subdir.
version_parts = meson.project_version().split('.')
build_info_conf = configuration_data()
build_info_conf.set('major', version_parts[0])
build_info_conf.set('minor', version_parts[1])
build_info_conf.set('patch', version_parts[2])
build_info_conf.set('flavor', '')
build_info_h = configure_file(
    input: '../build_info.h.tmpl',
    output: 'build_info.h',
    configuration: build_info_conf,
)
