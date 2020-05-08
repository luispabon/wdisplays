# SPDX-FileCopyrightText: 2020 Jason Francis <jason@cycles.network>
# SPDX-License-Identifier: CC0-1.0

project('network.cycles.wdisplays', 'c', license: 'MIT', version: '1.0')

conf = configuration_data()
conf.set('app_id', meson.project_name())
conf.set('version', meson.project_version())

subdir('protocol')
subdir('resources')
subdir('src')
