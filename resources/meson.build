# SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
# SPDX-License-Identifier: CC0-1.0

gnome = import('gnome')
resources = gnome.compile_resources(
  'waydisplay-resources', 'resources.xml',
  source_dir : '.',
  c_name : 'waydisplay_resources')

scour = find_program('scour', required: false)

icon = 'wdisplays.svg'
icondir = get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'

if scour.found()
  custom_target('optimize-icon',
      input: icon,
      output: '@0@.svg'.format(meson.project_name()),
      command: [scour,
      '--enable-viewboxing',
      '--enable-comment-stripping',
      '--remove-descriptive-elements',
      '--enable-id-stripping',
      '--shorten-ids',
      '--create-groups',
      '--strip-xml-space',
      '--strip-xml-prolog',
      '--indent=none',
      '--no-line-breaks',
      '@INPUT@', '@OUTPUT@'],
      install: true,
      install_dir: icondir)
  else
install_data(icon, install_dir: icondir)
  endif

install_data(
    configure_file(input: 'wdisplays.desktop.in',
      output: '@0@.desktop'.format(meson.project_name()),
      configuration: conf),
    install_dir: get_option('datadir') / 'applications')
