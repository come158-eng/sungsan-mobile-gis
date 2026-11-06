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
assert 'removeRecentProjectButton' in home and '#c52d26' in home and '#ffffff' in home
assert 'anchors.rightMargin: 70' in home and 'function removeFromRecent()' in home
assert 'Keys.onSpacePressed: removeFromRecent()' in home
assert 'Layout.preferredWidth: 44' not in home.split('id: deleteRecentButton', 1)[1].split('MouseArea {', 1)[0]
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
project_utils = (root / 'src/core/utils/projectutils.cpp').read_text(encoding='utf-8')
schema = project_utils.split('QgsFields objectFields;', 1)[1].split('QgsVectorFileWriter::SaveVectorOptions', 1)[0]
for name in ('gps_accuracy_m', 'landstar_id', 'landstar_code', 'northing', 'easting', 'elevation', 'fix_status'):
    assert '"' + name + '"' not in schema
assert 'compactFieldFormV2' in bridge and '!removedSurveyFields.contains( name )' in bridge
assert app.count('overlayFeatureFormDrawer.featureModel.stampFieldSurveyTime();') == 2
feature_model = (root / 'src/core/featuremodel.cpp').read_text(encoding='utf-8')
assert 'FID_IS_NULL( mFeature.id() ) || FID_IS_NEW( mFeature.id() )' in feature_model
camera = (root / 'src/qml/QFieldCamera.qml').read_text(encoding='utf-8')
assert "'/DCIM/capture_'" in camera and 'currentPath = UrlUtils.toLocalFile(String(path))' in camera
assert "captureToFile(qgisProject.homePath + '/DCIM/');" not in camera
photos = (root / 'src/qml/editorwidgets/ExternalResource.qml').read_text(encoding='utf-8')
assert 'storageResult.error' in photos and 'FileUtils.storeCameraPhoto' in photos
assert photos.index('if (!commitSungsanManagedPhoto(filepath))') < photos.index('platformUtilities.publishImageToGallery(finalPhotoPath, finalPhotoName)')
print('PASS: common names, compact form, preserved relations, edit/add separation, safe visible X')
