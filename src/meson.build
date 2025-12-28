liftoff_sources = [
  'main.c',
  'liftoff-application.c',
  'liftoff-window.c',
]

liftoff_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
]

liftoff_sources += gnome.compile_resources('liftoff-resources',
  'liftoff.gresource.xml',
  c_name: 'liftoff'
)

executable('liftoff', liftoff_sources,
  dependencies: liftoff_deps,
       install: true,
)
