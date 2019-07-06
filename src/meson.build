
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : false)
gdk = dependency('gdk-3.0')
gtk = dependency('gtk+-3.0')
assert(gdk.get_pkgconfig_variable('targets').split().contains('wayland'), 'Wayland GDK backend not present')

executable(
  'wdisplay',
  [
    'main.c',
    'outputs.c',
    resources,
  ],
  dependencies : [
    m_dep,
    wayland_client,
    client_protos,
    gtk
  ],
  install: true
)
