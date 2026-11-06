#!/usr/bin/env python3
"""Guard the project-ZIP entry point against the raw-dataset fallback."""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / 'src/core/qgismobileapp.cpp').read_text(encoding='utf-8')
load = source.split('bool QgisMobileapp::loadProjectFile(', 1)[1].split('void QgisMobileapp::reloadProjectFile()', 1)[0]
route = load.index('QgsZipUtils::files( path )')
assign = load.index('mProjectFilePath = path;')
assert route < assign, 'Project ZIP must be resolved before queuing raw dataset loading'
guard = load[route:assign]
for required in ('projects.size() != 1', 'QTemporaryDir imported', 'FileUtils::unzip(',
                 'imported.setAutoRemove( false )', 'return loadProjectFile( projectPath,',
                 'QDir::isAbsolutePath( entry )', 'QStringLiteral( "../" )'):
    assert required in guard, required
assert 'mProject->write(' not in guard, 'Import must not rewrite the supplied project'
print('PASS: project ZIP routing, ambiguity rejection, persistent relative paths, original-project preservation')
