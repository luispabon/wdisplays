
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)
rt_dep = cc.find_library('rt', required : false)
gdk = dependency('gdk-3.0')
gtk = dependency('gtk+-3.0')
assert(gdk.get_pkgconfig_variable('targets').split().contains('wayland'), 'Wayland GDK backend not present')
epoxy = dependency('epoxy')

executable(
  'wdisplays',
  [
    'main.c',
    'outputs.c',
    'render.c',
    'glviewport.c',
    resources,
  ],
  dependencies : [
    m_dep,
    rt_dep,
    wayland_client,
    client_protos,
    epoxy,
    gtk
  ],
  install: true
)
