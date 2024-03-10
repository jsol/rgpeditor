project('rpgeditor', 'c')

deps = []
deps += dependency('gio-2.0')
deps += dependency('gio-unix-2.0')
deps += dependency('glib-2.0')
deps += dependency('gtk4')
deps += dependency('libadwaita-1')

main_sources = files([
  'main.c',
  'editor_page.c'

])


executable('rpgeditor',
  sources: main_sources,
  dependencies : deps
  )
