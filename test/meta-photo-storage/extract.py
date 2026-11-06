from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

Path(sys.argv[2]).write_text('\n\n'.join(function(s) for s in (
    'bool FileUtils::isWithinProjectDirectory(',
    'bool FileUtils::replaceFileSafely(',
    'QVariantMap FileUtils::storeCameraPhoto(',
)), encoding='utf-8')
source = (Path(sys.argv[1]).parents[1] / 'featuremodel.cpp').read_text(encoding='utf-8')
Path(sys.argv[2]).with_name('production-fieldtime.inc').write_text(
    function('void FeatureModel::stampFieldSurveyTime()'), encoding='utf-8')
