from pathlib import Path

root = Path(__file__).resolve().parents[1]
app = (root / 'src/qml/qgismobileapp.qml').read_text(encoding='utf-8')
home = (root / 'src/qml/sungsan/SungsanHomeScreen.qml').read_text(encoding='utf-8')
bridge = (root / 'src/core/sungsansurveybridge.cpp').read_text(encoding='utf-8')
panel = (root / 'src/qml/sungsan/SungsanFieldPanel.qml').read_text(encoding='utf-8')
remove = app.split('onDeleteRecentProjectRequested:', 1)[1].split('onOpenRecentProjectRequested:', 1)[0]
assert 'removeRecentProject(path)' in remove
assert 'removeFolder' not in remove and 'clearProject' not in remove
assert 'ic_delete_forever_white_24dp' not in home
assert 'removeRecentProjectButton' in home and '#a82720' in home and '#fce7e3' in home
assert 'enabled: root.canAddFeature && !root.geometryInProgress' in panel
for start, end in [('onAddFeatureRequested:', 'onEditExistingFeatureRequested:'),
                   ('onStartSurveyRequested:', 'onCurrentLocationRequested:'),
                   ('onActiveLayerChanged:', 'onReturnHome:')]:
    handler = app.split(start, 1)[1].split(end, 1)[0]
    assert 'sungsanExistingFeatureEditPending = false' in handler
assert 'dashBoard.canEditLayerAttributes(layer)' in app
assert 'FlatLayerTreeModel.AttributeEditingLocked) === false' in app
assert 'layer->setName( QStringLiteral( "현장객체" ) )' in bridge
assert 'legacy.isValid()' in bridge and 'standardObjectLayer' in bridge
assert 'savedRelations' in bridge and 'element->clone( details )' in bridge
assert 'tr( "기본 입력" )' in bridge and 'tr( "상세정보" )' in bridge
print('PASS: common names, compact form, preserved relations, edit/add separation, safe visible X')
