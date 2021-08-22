project('artery-font', 'cpp',
    version: '1.0',
    license: 'MIT',
)

include_dirs = include_directories('.')
sources = []
dependencies = []

artery_font = library('artery-font',
    sources,
    dependencies: dependencies,
    include_directories: include_dirs,
)

artery_font_dep = declare_dependency(include_directories: include_dirs)
