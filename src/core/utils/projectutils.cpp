// Modified for Sungsan Mobile GIS by Sungsan on 2026-08-07.
/***************************************************************************
  projectutils.cpp - ProjectUtils

 ---------------------
 begin                : 19.04.2024
 copyright            : (C) 2024 by Mathieu Pellerin
 email                : mathieu@opengis.ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "layerutils.h"
#include "platformutilities.h"
#include "positioningutils.h"
#include "projectutils.h"

#include <qgsattributeeditorcontainer.h>
#include <qgsattributeeditorfield.h>
#include <qgsattributeeditorrelation.h>
#include <qgslayertree.h>
#include <qgsmaplayer.h>
#include <qgsprojectdisplaysettings.h>
#include <qgsrasterlayer.h>
#include <qgsrelation.h>
#include <qgsrelationcontext.h>
#include <qgsvectorfilewriter.h>
#include <qgsvectorlayer.h>
#include <qgsvectortilelayer.h>
#include <qgsvectortileutils.h>

#include <QJsonDocument>

ProjectUtils::ProjectUtils( QObject *parent )
  : QObject( parent )
{
}

QVariantMap ProjectUtils::mapLayers( QgsProject *project )
{
  if ( !project )
    return QVariantMap();

  QVariantMap mapLayers;
  const QMap<QString, QgsMapLayer *> projectMapLayers = project->mapLayers();
  for ( const QString &layerId : projectMapLayers.keys() )
  {
    mapLayers.insert( layerId, QVariant::fromValue<QgsMapLayer *>( projectMapLayers[layerId] ) );
  }

  return mapLayers;
}

bool ProjectUtils::isVWorldSatelliteLayer( QgsMapLayer *layer )
{
  if ( !layer )
    return false;

  const QString source = layer->source();
  return source.contains( QStringLiteral( "api.vworld.kr/req/wmts/" ), Qt::CaseInsensitive )
         && source.contains( QStringLiteral( "/Satellite/" ), Qt::CaseInsensitive );
}

bool ProjectUtils::addMapLayer( QgsProject *project, QgsMapLayer *layer )
{
  if ( !project )
    return false;

  return ( project->addMapLayer( layer ) );
}

bool ProjectUtils::addMapLayerAtBottom( QgsProject *project, QgsMapLayer *layer )
{
  if ( !project || !layer )
    return false;

  if ( isVWorldSatelliteLayer( layer ) )
  {
    layer->setAttribution( QStringLiteral( "출처: 국토교통부 브이월드(VWorld)" ) );
    layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/vworldSatellite" ), true );
  }

  if ( !project->addMapLayer( layer, false ) )
  {
    layer->deleteLater();
    return false;
  }

  if ( project->layerTreeRoot()->addLayer( layer ) )
    return true;

  project->removeMapLayer( layer );
  return false;
}

void ProjectUtils::removeMapLayer( QgsProject *project, QgsMapLayer *layer )
{
  if ( !project || !layer )
    return;

  project->removeMapLayer( layer );
}

void ProjectUtils::removeMapLayer( QgsProject *project, const QString &layerId )
{
  if ( !project || layerId.isEmpty() )
    return;

  project->removeMapLayer( layerId );
}

Qgis::TransactionMode ProjectUtils::transactionMode( QgsProject *project )
{
  if ( !project )
    return Qgis::TransactionMode::Disabled;

  return project->transactionMode();
}

QString ProjectUtils::title( QgsProject *project )
{
  if ( !project )
    return QString();

  const QString title = project->title();
  return !title.isEmpty() ? title : QFileInfo( project->fileName() ).completeBaseName();
}

QString ProjectUtils::createProject( const QVariantMap &options, const GnssPositionInformation &positionInformation )
{
  const bool sungsanFieldTemplate = options.value( QStringLiteral( "sungsan_field_template" ) ).toBool();
  QString projectTitle = options.value( QStringLiteral( "title" ), tr( "Created Project" ) ).toString();
  QString projectFilename = projectTitle.normalized( QString::NormalizationForm_KD );
  projectFilename.replace( QRegularExpression( "[^A-Za-z0-9_]" ), QStringLiteral( "_" ) );

  QDir createdProjectsDir( QStringLiteral( "%1/Created Projects/" ).arg( PlatformUtilities::instance()->applicationDirectory() ) );
  QString createdProjectDir = createdProjectsDir.filePath( projectFilename );
  int uniqueSuffix = 2;
  while ( QFileInfo::exists( createdProjectDir ) )
  {
    createdProjectDir = QStringLiteral( "%1_%2" ).arg( createdProjectsDir.filePath( projectFilename ), QString::number( uniqueSuffix++ ) );
  }
  createdProjectDir = QDir::cleanPath( createdProjectDir );
  createdProjectsDir.mkpath( createdProjectDir );
  const QString projectFilepath = QStringLiteral( "%1/%2.qgz" ).arg( createdProjectDir, projectFilename );

  QList<QgsMapLayer *> createdProjectLayers;
  QgsProject *createdProject = new QgsProject();

  // Metadata
  QgsProjectMetadata projectMetadata = createdProject->metadata();
  projectMetadata.setTitle( projectTitle );
  createdProject->setMetadata( projectMetadata );

  // Basic project settings
  const QgsCoordinateReferenceSystem defaultProjectCrs( QStringLiteral( "EPSG:3857" ) );
  createdProject->setCrs( defaultProjectCrs );
  createdProject->displaySettings()->setCoordinateType( Qgis::CoordinateDisplayType::CustomCrs );
  createdProject->displaySettings()->setCoordinateCustomCrs( QgsCoordinateReferenceSystem( "EPSG:4326" ) );

  if ( sungsanFieldTemplate )
  {
    createdProject->writeEntry( QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/fieldPackage" ), QStringLiteral( "kr.co.sungsan.mobilegis.field-package/1" ) );
    createdProject->writeEntry( QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/template" ), QStringLiteral( "field-landstar" ) );
    // A newly created generic project cannot know the coordinate system used
    // by a LandStar job.  Projected N/E imports stay locked until a desktop
    // field package records an explicit CRS confirmation.
    createdProject->writeEntry( QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/landstarCrsConfirmed" ), false );
    createdProject->writeEntry( QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/landstarCrsAuthId" ), defaultProjectCrs.authid() );
    createdProject->writeEntry( QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/landstarCrsNotice" ), QStringLiteral( "LandStar 좌표계는 현재 프로젝트 좌표계와 일치해야 합니다. 다르면 QGIS에서 확인 후 변환하세요." ) );
    createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "initialMapMode" ), QStringLiteral( "digitize" ) );
  }

  QgsVectorLayer *sungsanFieldObjectsLayer = nullptr;
  QgsVectorLayer *sungsanFieldPhotosLayer = nullptr;

  if ( sungsanFieldTemplate )
  {
    const QString fieldFilepath = QStringLiteral( "%1/sungsan_field.gpkg" ).arg( createdProjectDir );

    QgsFields objectFields;
    objectFields.append( QgsField( QStringLiteral( "object_id" ), QMetaType::QString, QString(), 40 ) );
    objectFields.append( QgsField( QStringLiteral( "name" ), QMetaType::QString, QString(), 100 ) );
    objectFields.append( QgsField( QStringLiteral( "category" ), QMetaType::QString, QString(), 40 ) );
    objectFields.append( QgsField( QStringLiteral( "memo" ), QMetaType::QString, QString(), 500 ) );
    objectFields.append( QgsField( QStringLiteral( "color" ), QMetaType::QString, QString(), 20 ) );
    objectFields.append( QgsField( QStringLiteral( "created_at" ), QMetaType::QDateTime ) );
    objectFields.append( QgsField( QStringLiteral( "gps_accuracy_m" ), QMetaType::Double, QString(), 12, 2 ) );
    objectFields.append( QgsField( QStringLiteral( "landstar_id" ), QMetaType::QString, QString(), 80 ) );
    objectFields.append( QgsField( QStringLiteral( "landstar_code" ), QMetaType::QString, QString(), 80 ) );
    objectFields.append( QgsField( QStringLiteral( "northing" ), QMetaType::Double, QString(), 18, 4 ) );
    objectFields.append( QgsField( QStringLiteral( "easting" ), QMetaType::Double, QString(), 18, 4 ) );
    objectFields.append( QgsField( QStringLiteral( "elevation" ), QMetaType::Double, QString(), 14, 4 ) );
    objectFields.append( QgsField( QStringLiteral( "fix_status" ), QMetaType::QString, QString(), 30 ) );
    objectFields.append( QgsField( QStringLiteral( "surveyed_at" ), QMetaType::QDateTime ) );
    objectFields.append( QgsField( QStringLiteral( "source_device" ), QMetaType::QString, QString(), 40 ) );
    // Kept for compatibility with existing packages and the LandStar bridge.
    // New captures use the relation-backed table below so their count is unlimited.
    objectFields.append( QgsField( QStringLiteral( "photo_near" ), QMetaType::QString, QString(), 500 ) );
    objectFields.append( QgsField( QStringLiteral( "photo_far" ), QMetaType::QString, QString(), 500 ) );
    objectFields.append( QgsField( QStringLiteral( "photo_other" ), QMetaType::QString, QString(), 500 ) );

    QgsVectorFileWriter::SaveVectorOptions objectWriterOptions;
    objectWriterOptions.driverName = QStringLiteral( "GPKG" );
    objectWriterOptions.layerName = QStringLiteral( "sungsan_field_objects" );
    QgsVectorFileWriter *objectWriter = QgsVectorFileWriter::create( fieldFilepath, objectFields, Qgis::WkbType::PointZ, defaultProjectCrs, createdProject->transformContext(), objectWriterOptions );
    delete objectWriter;

    const QString objectUri = QStringLiteral( "%1|layername=%2" ).arg( fieldFilepath, objectWriterOptions.layerName );
    sungsanFieldObjectsLayer = new QgsVectorLayer( objectUri, QStringLiteral( "성산_현장객체" ) );
    if ( !sungsanFieldObjectsLayer->isValid() )
    {
      delete sungsanFieldObjectsLayer;
      delete createdProject;
      return QString();
    }

    const QMap<QString, QString> objectAliases = {
      { QStringLiteral( "object_id" ), QStringLiteral( "객체 ID" ) },
      { QStringLiteral( "name" ), QStringLiteral( "객체명" ) },
      { QStringLiteral( "category" ), QStringLiteral( "종류" ) },
      { QStringLiteral( "memo" ), QStringLiteral( "메모" ) },
      { QStringLiteral( "color" ), QStringLiteral( "표시 색상" ) },
      { QStringLiteral( "created_at" ), QStringLiteral( "작성 시간" ) },
      { QStringLiteral( "gps_accuracy_m" ), QStringLiteral( "GPS 정확도(m)" ) },
      { QStringLiteral( "landstar_id" ), QStringLiteral( "LandStar 측점명" ) },
      { QStringLiteral( "landstar_code" ), QStringLiteral( "LandStar 코드" ) },
      { QStringLiteral( "northing" ), QStringLiteral( "북ing(N)" ) },
      { QStringLiteral( "easting" ), QStringLiteral( "동ing(E)" ) },
      { QStringLiteral( "elevation" ), QStringLiteral( "표고(Z)" ) },
      { QStringLiteral( "fix_status" ), QStringLiteral( "측량 고정상태" ) },
      { QStringLiteral( "surveyed_at" ), QStringLiteral( "측량 시간" ) },
      { QStringLiteral( "source_device" ), QStringLiteral( "측량 장비" ) },
      { QStringLiteral( "photo_near" ), QStringLiteral( "근경 사진" ) },
      { QStringLiteral( "photo_far" ), QStringLiteral( "원경 사진" ) },
      { QStringLiteral( "photo_other" ), QStringLiteral( "기타 사진" ) },
    };
    for ( auto aliasIterator = objectAliases.constBegin(); aliasIterator != objectAliases.constEnd(); ++aliasIterator )
    {
      const int fieldIndex = sungsanFieldObjectsLayer->fields().indexOf( aliasIterator.key() );
      if ( fieldIndex >= 0 )
        sungsanFieldObjectsLayer->setFieldAlias( fieldIndex, aliasIterator.value() );
    }

    const int objectIdIndex = sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "object_id" ) );
    const int createdAtIndex = sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "created_at" ) );
    const int colorIndex = sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "color" ) );
    const int accuracyIndex = sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "gps_accuracy_m" ) );
    const int sourceDeviceIndex = sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "source_device" ) );
    sungsanFieldObjectsLayer->setDefaultValueDefinition( objectIdIndex, QgsDefaultValue( QStringLiteral( "uuid()" ), false ) );
    sungsanFieldObjectsLayer->setDefaultValueDefinition( createdAtIndex, QgsDefaultValue( QStringLiteral( "now()" ), false ) );
    sungsanFieldObjectsLayer->setDefaultValueDefinition( colorIndex, QgsDefaultValue( QStringLiteral( "'#1f5aa6'" ), false ) );
    sungsanFieldObjectsLayer->setDefaultValueDefinition( accuracyIndex, QgsDefaultValue( QStringLiteral( "@gnss_horizontal_accuracy" ), false ) );
    sungsanFieldObjectsLayer->setDefaultValueDefinition( sourceDeviceIndex, QgsDefaultValue( QStringLiteral( "'성산 GIS'" ), false ) );

    QVariantMap emptyWidgetOptions;
    const QStringList hiddenObjectFields = { QStringLiteral( "fid" ), QStringLiteral( "object_id" ), QStringLiteral( "photo_near" ), QStringLiteral( "photo_far" ), QStringLiteral( "photo_other" ) };
    for ( const QString &fieldName : hiddenObjectFields )
    {
      const int fieldIndex = sungsanFieldObjectsLayer->fields().indexOf( fieldName );
      if ( fieldIndex >= 0 )
        sungsanFieldObjectsLayer->setEditorWidgetSetup( fieldIndex, QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), emptyWidgetOptions ) );
    }

    QVariantMap categoryOptions;
    categoryOptions.insert( QStringLiteral( "map" ), QVariantList{
                                                       QVariantMap{ { QStringLiteral( "확인 지점" ), QStringLiteral( "확인" ) } },
                                                       QVariantMap{ { QStringLiteral( "시설물" ), QStringLiteral( "시설물" ) } },
                                                       QVariantMap{ { QStringLiteral( "위험·주의" ), QStringLiteral( "위험" ) } },
                                                       QVariantMap{ { QStringLiteral( "보수 필요" ), QStringLiteral( "보수" ) } },
                                                       QVariantMap{ { QStringLiteral( "기타" ), QStringLiteral( "기타" ) } } } );
    sungsanFieldObjectsLayer->setEditorWidgetSetup( sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "category" ) ), QgsEditorWidgetSetup( QStringLiteral( "ValueMap" ), categoryOptions ) );

    QVariantMap colorOptions;
    colorOptions.insert( QStringLiteral( "map" ), QVariantList{
                                                    QVariantMap{ { QStringLiteral( "파랑" ), QStringLiteral( "#1f5aa6" ) } },
                                                    QVariantMap{ { QStringLiteral( "빨강" ), QStringLiteral( "#d83b3b" ) } },
                                                    QVariantMap{ { QStringLiteral( "주황" ), QStringLiteral( "#e67e22" ) } },
                                                    QVariantMap{ { QStringLiteral( "노랑" ), QStringLiteral( "#d4ac0d" ) } },
                                                    QVariantMap{ { QStringLiteral( "초록" ), QStringLiteral( "#239b56" ) } },
                                                    QVariantMap{ { QStringLiteral( "보라" ), QStringLiteral( "#7d3c98" ) } },
                                                    QVariantMap{ { QStringLiteral( "검정" ), QStringLiteral( "#2c3e50" ) } } } );
    sungsanFieldObjectsLayer->setEditorWidgetSetup( colorIndex, QgsEditorWidgetSetup( QStringLiteral( "ValueMap" ), colorOptions ) );

    QVariantMap multilineOptions;
    multilineOptions.insert( QStringLiteral( "IsMultiline" ), true );
    multilineOptions.insert( QStringLiteral( "UseHtml" ), false );
    sungsanFieldObjectsLayer->setEditorWidgetSetup( sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "memo" ) ), QgsEditorWidgetSetup( QStringLiteral( "TextEdit" ), multilineOptions ) );

    QVariantMap dateTimeOptions;
    dateTimeOptions.insert( QStringLiteral( "field_format" ), QStringLiteral( "yyyy-MM-dd HH:mm:ss" ) );
    sungsanFieldObjectsLayer->setEditorWidgetSetup( createdAtIndex, QgsEditorWidgetSetup( QStringLiteral( "DateTime" ), dateTimeOptions ) );
    sungsanFieldObjectsLayer->setEditorWidgetSetup( sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "surveyed_at" ) ), QgsEditorWidgetSetup( QStringLiteral( "DateTime" ), dateTimeOptions ) );

    sungsanFieldObjectsLayer->setDisplayExpression( QStringLiteral( "coalesce(nullif(trim(\"landstar_id\"), ''), nullif(trim(\"name\"), ''), \"category\", '현장 객체')" ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldObjects" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/landstarImportTarget" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPackage" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "QFieldSync/cloud_action" ), QStringLiteral( "offline" ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "QFieldSync/action" ), QStringLiteral( "offline" ) );
    LayerUtils::setDefaultRenderer( sungsanFieldObjectsLayer, nullptr, QString(), QStringLiteral( "color" ) );

    QgsFields photoFields;
    photoFields.append( QgsField( QStringLiteral( "photo_id" ), QMetaType::QString, QString(), 40 ) );
    photoFields.append( QgsField( QStringLiteral( "object_id" ), QMetaType::QString, QString(), 40 ) );
    photoFields.append( QgsField( QStringLiteral( "point_name" ), QMetaType::QString, QString(), 100 ) );
    photoFields.append( QgsField( QStringLiteral( "photo_type" ), QMetaType::QString, QString(), 20 ) );
    photoFields.append( QgsField( QStringLiteral( "sequence" ), QMetaType::Int ) );
    photoFields.append( QgsField( QStringLiteral( "media" ), QMetaType::QString, QString(), 500 ) );
    photoFields.append( QgsField( QStringLiteral( "captured_at" ), QMetaType::QDateTime ) );
    photoFields.append( QgsField( QStringLiteral( "memo" ), QMetaType::QString, QString(), 500 ) );

    QgsVectorFileWriter::SaveVectorOptions photoWriterOptions;
    photoWriterOptions.driverName = QStringLiteral( "GPKG" );
    photoWriterOptions.layerName = QStringLiteral( "sungsan_field_photos" );
    photoWriterOptions.actionOnExistingFile = QgsVectorFileWriter::CreateOrOverwriteLayer;
    QgsVectorFileWriter *photoWriter = QgsVectorFileWriter::create( fieldFilepath, photoFields, Qgis::WkbType::NoGeometry, QgsCoordinateReferenceSystem(), createdProject->transformContext(), photoWriterOptions );
    delete photoWriter;

    const QString photoUri = QStringLiteral( "%1|layername=%2" ).arg( fieldFilepath, photoWriterOptions.layerName );
    sungsanFieldPhotosLayer = new QgsVectorLayer( photoUri, QStringLiteral( "성산_현장사진" ) );
    if ( !sungsanFieldPhotosLayer->isValid() )
    {
      delete sungsanFieldObjectsLayer;
      delete sungsanFieldPhotosLayer;
      delete createdProject;
      return QString();
    }

    const QMap<QString, QString> photoAliases = {
      { QStringLiteral( "photo_id" ), QStringLiteral( "사진 ID" ) },
      { QStringLiteral( "object_id" ), QStringLiteral( "연결 객체 ID" ) },
      { QStringLiteral( "point_name" ), QStringLiteral( "LandStar 타점명" ) },
      { QStringLiteral( "photo_type" ), QStringLiteral( "사진 종류" ) },
      { QStringLiteral( "sequence" ), QStringLiteral( "사진 순번" ) },
      { QStringLiteral( "media" ), QStringLiteral( "사진" ) },
      { QStringLiteral( "captured_at" ), QStringLiteral( "촬영 시간" ) },
      { QStringLiteral( "memo" ), QStringLiteral( "사진 메모" ) },
    };
    for ( auto aliasIterator = photoAliases.constBegin(); aliasIterator != photoAliases.constEnd(); ++aliasIterator )
    {
      const int fieldIndex = sungsanFieldPhotosLayer->fields().indexOf( aliasIterator.key() );
      if ( fieldIndex >= 0 )
        sungsanFieldPhotosLayer->setFieldAlias( fieldIndex, aliasIterator.value() );
    }

    const int photoIdIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "photo_id" ) );
    const int photoObjectIdIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "object_id" ) );
    const int pointNameIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "point_name" ) );
    const int photoTypeIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "photo_type" ) );
    const int sequenceIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "sequence" ) );
    const int mediaIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "media" ) );
    const int capturedAtIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "captured_at" ) );
    const int photoMemoIndex = sungsanFieldPhotosLayer->fields().indexOf( QStringLiteral( "memo" ) );
    sungsanFieldPhotosLayer->setDefaultValueDefinition( photoIdIndex, QgsDefaultValue( QStringLiteral( "uuid()" ), false ) );
    sungsanFieldPhotosLayer->setDefaultValueDefinition( photoObjectIdIndex, QgsDefaultValue( QStringLiteral( "attribute(@parent, 'object_id')" ), false ) );
    sungsanFieldPhotosLayer->setDefaultValueDefinition( pointNameIndex, QgsDefaultValue( QStringLiteral( "coalesce(nullif(trim(attribute(@parent, 'landstar_id')), ''), nullif(trim(attribute(@parent, 'name')), ''), nullif(trim(attribute(@parent, 'object_id')), ''), 'POINT')" ), false ) );
    sungsanFieldPhotosLayer->setDefaultValueDefinition( photoTypeIndex, QgsDefaultValue( QStringLiteral( "'추가'" ), false ) );
    sungsanFieldPhotosLayer->setDefaultValueDefinition( sequenceIndex, QgsDefaultValue( QStringLiteral( "coalesce(aggregate('성산_현장사진', 'max', \"sequence\", \"object_id\" = attribute(@parent, 'object_id')), 0) + 1" ), false ) );
    sungsanFieldPhotosLayer->setDefaultValueDefinition( capturedAtIndex, QgsDefaultValue( QStringLiteral( "now()" ), false ) );

    for ( const QString &fieldName : { QStringLiteral( "fid" ), QStringLiteral( "photo_id" ), QStringLiteral( "object_id" ) } )
    {
      const int fieldIndex = sungsanFieldPhotosLayer->fields().indexOf( fieldName );
      if ( fieldIndex >= 0 )
        sungsanFieldPhotosLayer->setEditorWidgetSetup( fieldIndex, QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), emptyWidgetOptions ) );
    }

    QVariantMap photoTypeOptions;
    photoTypeOptions.insert( QStringLiteral( "map" ), QVariantList{
                                                        QVariantMap{ { QStringLiteral( "근경" ), QStringLiteral( "근경" ) } },
                                                        QVariantMap{ { QStringLiteral( "원경" ), QStringLiteral( "원경" ) } },
                                                        QVariantMap{ { QStringLiteral( "기타" ), QStringLiteral( "기타" ) } },
                                                        QVariantMap{ { QStringLiteral( "추가" ), QStringLiteral( "추가" ) } } } );
    sungsanFieldPhotosLayer->setEditorWidgetSetup( photoTypeIndex, QgsEditorWidgetSetup( QStringLiteral( "ValueMap" ), photoTypeOptions ) );

    QVariantMap mediaOptions;
    mediaOptions.insert( QStringLiteral( "DocumentViewer" ), 1 );
    mediaOptions.insert( QStringLiteral( "FileWidget" ), true );
    mediaOptions.insert( QStringLiteral( "FileWidgetButton" ), true );
    mediaOptions.insert( QStringLiteral( "RelativeStorage" ), 1 );
    mediaOptions.insert( QStringLiteral( "StorageMode" ), 0 );
    mediaOptions.insert( QStringLiteral( "UseLink" ), false );
    sungsanFieldPhotosLayer->setEditorWidgetSetup( mediaIndex, QgsEditorWidgetSetup( QStringLiteral( "ExternalResource" ), mediaOptions ) );
    sungsanFieldPhotosLayer->setEditorWidgetSetup( capturedAtIndex, QgsEditorWidgetSetup( QStringLiteral( "DateTime" ), dateTimeOptions ) );
    sungsanFieldPhotosLayer->setEditorWidgetSetup( photoMemoIndex, QgsEditorWidgetSetup( QStringLiteral( "TextEdit" ), multilineOptions ) );
    sungsanFieldPhotosLayer->setDisplayExpression( QStringLiteral( "coalesce(\"point_name\", 'POINT') || ' · ' || coalesce(\"photo_type\", '추가') || ' #' || coalesce(\"sequence\", 1)" ) );
    sungsanFieldPhotosLayer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotos" ), true );
    sungsanFieldPhotosLayer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPackage" ), true );
    sungsanFieldPhotosLayer->setCustomProperty( QStringLiteral( "QFieldSync/cloud_action" ), QStringLiteral( "offline" ) );
    sungsanFieldPhotosLayer->setCustomProperty( QStringLiteral( "QFieldSync/action" ), QStringLiteral( "offline" ) );

    const QString attachmentNamingExpression = QStringLiteral(
      "with_variable('point_name_safe', "
      "regexp_replace(coalesce(nullif(trim(\"point_name\"), ''), 'POINT'), '[^0-9A-Za-z가-힣._-]+', '_'), "
      "with_variable('photo_type_sanitized', "
      "regexp_replace(coalesce(nullif(trim(\"photo_type\"), ''), '추가'), '[^0-9A-Za-z가-힣._-]+', '_'), "
      "with_variable('photo_type_safe', "
      "CASE WHEN @photo_type_sanitized IN ('근경', '원경', '기타', '추가') "
      "THEN @photo_type_sanitized ELSE '추가' END, "
      "'photos/' || @photo_type_safe || '/' || "
      "@point_name_safe || '_' || @photo_type_safe || '_' || "
      "lpad(to_string(coalesce(\"sequence\", 1)), 3, '0') || '_' || "
      "left(replace(coalesce(nullif(trim(\"photo_id\"), ''), uuid()), '-', ''), 8) || '.{extension}')))" );
    QVariantMap attachmentNaming;
    attachmentNaming.insert( QStringLiteral( "media" ), attachmentNamingExpression );
    sungsanFieldPhotosLayer->setCustomProperty( QStringLiteral( "QFieldSync/attachment_naming" ), QString::fromUtf8( QJsonDocument::fromVariant( attachmentNaming ).toJson( QJsonDocument::Compact ) ) );
    sungsanFieldPhotosLayer->setFlags( sungsanFieldPhotosLayer->flags() | QgsMapLayer::Private );

    QgsEditFormConfig objectFormConfig = sungsanFieldObjectsLayer->editFormConfig();
    objectFormConfig.clearTabs();
    objectFormConfig.setLayout( Qgis::AttributeFormLayout::DragAndDrop );
    QgsAttributeEditorContainer *objectFormRoot = objectFormConfig.invisibleRootContainer();
    const QStringList orderedObjectFields = {
      QStringLiteral( "name" ), QStringLiteral( "landstar_id" ), QStringLiteral( "landstar_code" ),
      QStringLiteral( "category" ), QStringLiteral( "memo" ), QStringLiteral( "color" ),
      QStringLiteral( "northing" ), QStringLiteral( "easting" ), QStringLiteral( "elevation" ),
      QStringLiteral( "fix_status" ), QStringLiteral( "gps_accuracy_m" ), QStringLiteral( "surveyed_at" ),
      QStringLiteral( "source_device" ), QStringLiteral( "created_at" ) };
    for ( const QString &fieldName : orderedObjectFields )
    {
      const int fieldIndex = sungsanFieldObjectsLayer->fields().indexOf( fieldName );
      if ( fieldIndex >= 0 )
        objectFormRoot->addChildElement( new QgsAttributeEditorField( fieldName, fieldIndex, objectFormRoot ) );
    }
    QgsAttributeEditorRelation *photoRelationElement = new QgsAttributeEditorRelation( QStringLiteral( "sungsan_field_photos" ), objectFormRoot );
    photoRelationElement->setLabel( QStringLiteral( "현장 사진 · 근경/원경/기타/추가" ) );
    objectFormRoot->addChildElement( photoRelationElement );
    sungsanFieldObjectsLayer->setEditFormConfig( objectFormConfig );

    QDir( createdProjectDir ).mkpath( QStringLiteral( "photos/근경" ) );
    QDir( createdProjectDir ).mkpath( QStringLiteral( "photos/원경" ) );
    QDir( createdProjectDir ).mkpath( QStringLiteral( "photos/기타" ) );
    QDir( createdProjectDir ).mkpath( QStringLiteral( "photos/추가" ) );

    createdProjectLayers << sungsanFieldObjectsLayer << sungsanFieldPhotosLayer;
  }

  // Notes-related layers
  QgsVectorLayer *notesPointLayer = nullptr;
  QgsVectorLayer *notesLineLayer = nullptr;
  QgsVectorLayer *notesPolygonLayer = nullptr;
  QgsVectorLayer *attachmentsLayer = nullptr;
  if ( options.value( QStringLiteral( "notes" ) ).toBool() )
  {
    QList<QgsVectorLayer *> notesLayers;

    createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "initialMapMode" ), QStringLiteral( "digitize" ) );

    const QString notesFilepath = QStringLiteral( "%1/notes.gpkg" ).arg( createdProjectDir );
    const bool notesHasAdditionalGeometries = options.value( QStringLiteral( "notes_on_lines_polygons" ) ).toBool();
    const bool notesHasAttachments = options.value( QStringLiteral( "camera_capture" ) ).toBool();

    QgsFields notesFields;
    notesFields.append( QgsField( QStringLiteral( "uuid" ), QMetaType::QString ) );
    notesFields.append( QgsField( QStringLiteral( "color" ), QMetaType::QString ) );
    notesFields.append( QgsField( QStringLiteral( "title" ), QMetaType::QString ) );
    notesFields.append( QgsField( QStringLiteral( "note" ), QMetaType::QString ) );
    notesFields.append( QgsField( QStringLiteral( "timestamp" ), QMetaType::QDateTime ) );

    QgsVectorFileWriter::SaveVectorOptions writerOptions;
    writerOptions.layerName = "notes_point";
    QgsVectorFileWriter *writer = QgsVectorFileWriter::create( notesFilepath, notesFields, Qgis::WkbType::PointZ, QgsCoordinateReferenceSystem( "EPSG:4326" ), createdProject->transformContext(), writerOptions );
    delete writer;

    writerOptions.actionOnExistingFile = QgsVectorFileWriter::CreateOrOverwriteLayer;

    notesPointLayer = new QgsVectorLayer( QStringLiteral( "%1|layername=%2" ).arg( notesFilepath, writerOptions.layerName ), notesHasAdditionalGeometries ? QStringLiteral( "%1 — %2" ).arg( tr( "Notes" ), tr( "Point" ) ) : tr( "Notes" ) );
    notesLayers << notesPointLayer;

    LayerUtils::setDefaultRenderer( notesPointLayer, nullptr,
                                    options.value( QStringLiteral( "camera_capture" ) ).toBool() ? QStringLiteral( "relation_aggregate('notes_attachments_relation_%1', 'max', \"media\")" ).arg( notesPointLayer->id() ) : QString(),
                                    QStringLiteral( "color" ) );
    LayerUtils::setDefaultLabeling( notesPointLayer );

    if ( notesHasAdditionalGeometries )
    {
      writerOptions.layerName = "notes_line";
      writer = QgsVectorFileWriter::create( notesFilepath, notesFields, Qgis::WkbType::LineStringZ, QgsCoordinateReferenceSystem( "EPSG:4326" ), createdProject->transformContext(), writerOptions );
      delete writer;

      notesLineLayer = new QgsVectorLayer( QStringLiteral( "%1|layername=%2" ).arg( notesFilepath, writerOptions.layerName ), QStringLiteral( "%1 — %2" ).arg( tr( "Notes" ), tr( "Line" ) ) );
      notesLayers << notesLineLayer;

      LayerUtils::setDefaultRenderer( notesLineLayer, nullptr, QString(), QStringLiteral( "color" ) );

      writerOptions.layerName = "notes_polygon";
      writer = QgsVectorFileWriter::create( notesFilepath, notesFields, Qgis::WkbType::PolygonZ, QgsCoordinateReferenceSystem( "EPSG:4326" ), createdProject->transformContext(), writerOptions );
      delete writer;

      notesPolygonLayer = new QgsVectorLayer( QStringLiteral( "%1|layername=%2" ).arg( notesFilepath, writerOptions.layerName ), QStringLiteral( "%1 — %2" ).arg( tr( "Notes" ), tr( "Polygon" ) ) );
      notesLayers << notesPolygonLayer;

      LayerUtils::setDefaultRenderer( notesPolygonLayer, nullptr, QString(), QStringLiteral( "color" ) );
    }

    if ( notesHasAttachments )
    {
      // Second layer in the same notes.gpkg
      QgsFields attachFields;
      attachFields.append( QgsField( QStringLiteral( "note_layer" ), QMetaType::QString ) );
      attachFields.append( QgsField( QStringLiteral( "note_uuid" ), QMetaType::QString ) );
      attachFields.append( QgsField( QStringLiteral( "media" ), QMetaType::QString ) );
      attachFields.append( QgsField( QStringLiteral( "description" ), QMetaType::QString ) );
      attachFields.append( QgsField( QStringLiteral( "timestamp" ), QMetaType::QDateTime ) );

      writerOptions.layerName = QStringLiteral( "notes_attachments" );
      QgsVectorFileWriter *attachWriter = QgsVectorFileWriter::create( notesFilepath, attachFields, Qgis::WkbType::NoGeometry, QgsCoordinateReferenceSystem(), createdProject->transformContext(), writerOptions );
      delete attachWriter;

      const QString attachUri = QStringLiteral( "%1|layername=%2" ).arg( notesFilepath, writerOptions.layerName );
      attachmentsLayer = new QgsVectorLayer( attachUri, tr( "Note attachments" ) );
      QgsFields liveAttachFields = attachmentsLayer->fields();

      int attachFieldIndex;
      QVariantMap attachWidgetOptions;
      QgsEditorWidgetSetup attachWidgetSetup;

      // Hide fid
      attachFieldIndex = liveAttachFields.indexOf( QStringLiteral( "fid" ) );
      if ( attachFieldIndex >= 0 )
      {
        attachWidgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), QVariantMap() );
        attachmentsLayer->setEditorWidgetSetup( attachFieldIndex, attachWidgetSetup );
      }

      attachFieldIndex = liveAttachFields.indexOf( QStringLiteral( "note_layer" ) );
      if ( attachFieldIndex >= 0 )
      {
        attachWidgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), QVariantMap() );
        attachmentsLayer->setEditorWidgetSetup( attachFieldIndex, attachWidgetSetup );
      }

      attachFieldIndex = liveAttachFields.indexOf( QStringLiteral( "note_uuid" ) );
      if ( attachFieldIndex >= 0 )
      {
        attachWidgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), QVariantMap() );
        attachmentsLayer->setEditorWidgetSetup( attachFieldIndex, attachWidgetSetup );
      }
      attachFieldIndex = liveAttachFields.indexOf( QStringLiteral( "media" ) );
      if ( attachFieldIndex >= 0 )
      {
        attachWidgetOptions.clear();
        attachWidgetOptions[QStringLiteral( "DocumentViewer" )] = 1;
        attachWidgetOptions[QStringLiteral( "RelativeStorage" )] = 1;
        attachWidgetOptions[QStringLiteral( "FileWidget" )] = true;
        attachWidgetOptions[QStringLiteral( "FileWidgetButton" )] = true;
        attachWidgetSetup = QgsEditorWidgetSetup( QStringLiteral( "ExternalResource" ), attachWidgetOptions );
        attachmentsLayer->setEditorWidgetSetup( attachFieldIndex, attachWidgetSetup );
        attachmentsLayer->setFieldAlias( attachFieldIndex, tr( "Media" ) );
      }
      attachFieldIndex = liveAttachFields.indexOf( QStringLiteral( "description" ) );
      if ( attachFieldIndex >= 0 )
      {
        attachWidgetOptions.clear();
        attachWidgetOptions[QStringLiteral( "IsMultiline" )] = true;
        attachWidgetSetup = QgsEditorWidgetSetup( QStringLiteral( "TextEdit" ), attachWidgetOptions );
        attachmentsLayer->setEditorWidgetSetup( attachFieldIndex, attachWidgetSetup );
        attachmentsLayer->setFieldAlias( attachFieldIndex, tr( "Description" ) );
      }
      attachFieldIndex = liveAttachFields.indexOf( QStringLiteral( "timestamp" ) );
      if ( attachFieldIndex >= 0 )
      {
        attachWidgetOptions.clear();
        attachWidgetOptions[QStringLiteral( "display_format" )] = QStringLiteral( "yyyy-MM-dd HH:mm" );
        attachWidgetOptions[QStringLiteral( "field_format" )] = QStringLiteral( "yyyy-MM-dd HH:mm" );
        attachWidgetOptions[QStringLiteral( "field_format_overwrite" )] = true;
        attachWidgetOptions[QStringLiteral( "allow_null" )] = true;
        attachWidgetOptions[QStringLiteral( "calendar_popup" )] = true;
        attachWidgetSetup = QgsEditorWidgetSetup( QStringLiteral( "DateTime" ), attachWidgetOptions );
        attachmentsLayer->setEditorWidgetSetup( attachFieldIndex, attachWidgetSetup );
        attachmentsLayer->setDefaultValueDefinition( attachFieldIndex, QgsDefaultValue( QStringLiteral( "now()" ), false ) );
        attachmentsLayer->setFieldAlias( attachFieldIndex, tr( "Time" ) );
      }

      attachmentsLayer->setDisplayExpression( QStringLiteral( "COALESCE(\"media\", 'Attachment #' || fid)" ) );
      QgsEditFormConfig attachFormConfig = attachmentsLayer->editFormConfig();
      attachFormConfig.setSuppress( Qgis::AttributeFormSuppression::On );
      attachmentsLayer->setEditFormConfig( attachFormConfig );
      attachmentsLayer->setCustomProperty( QStringLiteral( "QFieldSync/cloud_action" ), QStringLiteral( "offline" ) );
      attachmentsLayer->setCustomProperty( QStringLiteral( "QFieldSync/action" ), QStringLiteral( "offline" ) );

      attachmentsLayer->setFlags( attachmentsLayer->flags() | QgsMapLayer::Private );

      createdProjectLayers << attachmentsLayer;
    }

    for ( QgsVectorLayer *notesLayer : notesLayers )
    {
      QgsFields fields = notesLayer->fields();
      // Set a nice display expression for the feature list
      notesLayer->setDisplayExpression( "COALESCE( title , 'Note #' || fid || ' from ' || format_date( timestamp, 'yyyy-MM-dd HH:mm' ) )" );

      int fieldIndex;
      QVariantMap widgetOptions;
      QgsEditorWidgetSetup widgetSetup;

      // Configure fid field
      fieldIndex = fields.indexOf( QStringLiteral( "fid" ) );
      if ( fieldIndex >= 0 )
      {
        widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), widgetOptions );
        notesLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
      }

      // Configure uuid field (referenced key for relations)
      fieldIndex = fields.indexOf( QStringLiteral( "uuid" ) );
      if ( fieldIndex >= 0 )
      {
        widgetOptions.clear();
        widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), widgetOptions );
        notesLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
        notesLayer->setDefaultValueDefinition( fieldIndex, QgsDefaultValue( QStringLiteral( "uuid()" ), false ) );
      }

      // Configure time field
      fieldIndex = fields.indexOf( QStringLiteral( "timestamp" ) );
      if ( fieldIndex >= 0 )
      {
        widgetOptions.clear();
        widgetOptions[QStringLiteral( "display_format" )] = QStringLiteral( "yyyy-MM-dd HH:mm" );
        widgetOptions[QStringLiteral( "field_format" )] = QStringLiteral( "yyyy-MM-dd HH:mm" );
        widgetOptions[QStringLiteral( "field_format_overwrite" )] = true;
        widgetOptions[QStringLiteral( "allow_null" )] = true;
        widgetOptions[QStringLiteral( "calendar_popup" )] = true;
        widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "DateTime" ), widgetOptions );
        notesLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
        notesLayer->setDefaultValueDefinition( fieldIndex, QgsDefaultValue( QStringLiteral( "now()" ), false ) );
        notesLayer->setFieldAlias( fieldIndex, tr( "Time" ) );
      }

      // Configure color field
      fieldIndex = fields.indexOf( QStringLiteral( "color" ) );
      if ( fieldIndex >= 0 )
      {
        widgetOptions.clear();
        widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Color" ), widgetOptions );
        notesLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
        notesLayer->setDefaultValueDefinition( fieldIndex, QgsDefaultValue( QStringLiteral( "'#377eb8'" ), false ) );
        notesLayer->setFieldAlias( fieldIndex, tr( "Marker color" ) );
      }

      // Configure note field
      fieldIndex = fields.indexOf( QStringLiteral( "title" ) );
      if ( fieldIndex >= 0 )
      {
        widgetOptions.clear();
        widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "TextEdit" ), widgetOptions );
        notesLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
        notesLayer->setFieldAlias( fieldIndex, tr( "Title" ) );
      }

      // Configure note field
      fieldIndex = fields.indexOf( QStringLiteral( "note" ) );
      if ( fieldIndex >= 0 )
      {
        widgetOptions.clear();
        widgetOptions[QStringLiteral( "IsMultiline" )] = true;
        widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "TextEdit" ), widgetOptions );
        notesLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
        notesLayer->setFieldAlias( fieldIndex, tr( "Note" ) );
      }

      // Insure the layer is ready cloud-friendly
      notesLayer->setCustomProperty( QStringLiteral( "QFieldSync/cloud_action" ), QStringLiteral( "offline" ) );
      notesLayer->setCustomProperty( QStringLiteral( "QFieldSync/action" ), QStringLiteral( "offline" ) );

      notesLayer->setDisplayExpression( QStringLiteral( "\"title\"" ) );

      createdProjectLayers << notesLayer;

      if ( notesHasAttachments )
      {
        QgsEditFormConfig notesFormConfig = notesLayer->editFormConfig();
        notesFormConfig.clearTabs();
        notesFormConfig.setLayout( Qgis::AttributeFormLayout::DragAndDrop );
        QgsAttributeEditorContainer *root = notesFormConfig.invisibleRootContainer();
        QgsAttributeEditorRelation *relationElement = new QgsAttributeEditorRelation( QStringLiteral( "notes_attachments_relation_%1" ).arg( notesLayer->id() ), root );
        root->addChildElement( relationElement );
        const QStringList orderedFields = {
          QStringLiteral( "color" ),
          QStringLiteral( "title" ),
          QStringLiteral( "note" ),
          QStringLiteral( "timestamp" ) };
        for ( const QString &fieldName : orderedFields )
        {
          const int idx = notesLayer->fields().indexOf( fieldName );
          if ( idx >= 0 )
          {
            root->addChildElement( new QgsAttributeEditorField( fieldName, idx, root ) );
          }
        }

        notesLayer->setEditFormConfig( notesFormConfig );
      }
    }
  }

  // Tracks layer
  QgsVectorLayer *tracksLayer = nullptr;
  if ( options.value( QStringLiteral( "tracks" ) ).toBool() )
  {
    const QString tracksFilepath = QStringLiteral( "%1/tracks.gpkg" ).arg( createdProjectDir );

    QgsFields fields;
    fields.append( QgsField( QStringLiteral( "color" ), QMetaType::QString ) );
    fields.append( QgsField( QStringLiteral( "title" ), QMetaType::QString ) );
    fields.append( QgsField( QStringLiteral( "timestamp" ), QMetaType::QDateTime ) );
    QgsVectorFileWriter::SaveVectorOptions writerOptions;
    QgsVectorFileWriter *writer = QgsVectorFileWriter::create( tracksFilepath, fields, Qgis::WkbType::LineStringZM, QgsCoordinateReferenceSystem( "EPSG:4326" ), createdProject->transformContext(), writerOptions );
    delete writer;

    tracksLayer = new QgsVectorLayer( tracksFilepath, tr( "Tracks" ) );
    fields = tracksLayer->fields();
    LayerUtils::setDefaultRenderer( tracksLayer, nullptr, QString(), QStringLiteral( "color" ) );

    // Set a nice display expression for the feature list
    tracksLayer->setDisplayExpression( "'Track #' || fid || ' from ' || format_date( timestamp, 'yyyy-MM-dd HH:mm' )" );

    int fieldIndex;
    QVariantMap widgetOptions;
    QgsEditorWidgetSetup widgetSetup;

    // Configure fid field
    fieldIndex = fields.indexOf( QStringLiteral( "fid" ) );
    if ( fieldIndex >= 0 )
    {
      widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Hidden" ), widgetOptions );
      tracksLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
    }

    // Configure color field
    fieldIndex = fields.indexOf( QStringLiteral( "color" ) );
    if ( fieldIndex >= 0 )
    {
      widgetOptions.clear();
      widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "Color" ), widgetOptions );
      tracksLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
      tracksLayer->setDefaultValueDefinition( fieldIndex, QgsDefaultValue( QStringLiteral( "'#377eb8'" ), false ) );
      tracksLayer->setFieldAlias( fieldIndex, tr( "Track color" ) );
    }

    // Configure note field
    fieldIndex = fields.indexOf( QStringLiteral( "title" ) );
    if ( fieldIndex >= 0 )
    {
      widgetOptions.clear();
      widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "TextEdit" ), widgetOptions );
      tracksLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
      tracksLayer->setFieldAlias( fieldIndex, tr( "Title" ) );
    }

    // Configure time field
    fieldIndex = fields.indexOf( QStringLiteral( "timestamp" ) );
    if ( fieldIndex >= 0 )
    {
      widgetOptions.clear();
      widgetOptions[QStringLiteral( "display_format" )] = QStringLiteral( "yyyy-MM-dd HH:mm" );
      widgetOptions[QStringLiteral( "field_format" )] = QStringLiteral( "yyyy-MM-dd HH:mm" );
      widgetOptions[QStringLiteral( "field_format_overwrite" )] = true;
      widgetOptions[QStringLiteral( "allow_null" )] = true;
      widgetOptions[QStringLiteral( "calendar_popup" )] = true;
      widgetSetup = QgsEditorWidgetSetup( QStringLiteral( "DateTime" ), widgetOptions );
      tracksLayer->setEditorWidgetSetup( fieldIndex, widgetSetup );
      tracksLayer->setDefaultValueDefinition( fieldIndex, QgsDefaultValue( QStringLiteral( "now()" ), false ) );
      tracksLayer->setFieldAlias( fieldIndex, tr( "Time" ) );
    }

    if ( options.value( QStringLiteral( "track_on_launch" ) ).toBool() )
    {
      // Skip feature form when launching tracks
      QgsEditFormConfig formConfig = tracksLayer->editFormConfig();
      formConfig.setSuppress( Qgis::AttributeFormSuppression::On );
      tracksLayer->setEditFormConfig( formConfig );

      // Launch tracks on project opening
      tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/tracking_session_active" ), true );
      tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/tracking_time_requirement_active" ), true );
      tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/tracking_time_requirement_interval_seconds" ), 2 );
      tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/tracking_erroneous_distance_safeguard_active" ), true );
      tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/tracking_erroneous_distance_safeguard_maximum_meters" ), 50 );
      tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/tracking_measurement_type" ), 1 ); // Attach epoch value to the M value
    }
    else
    {
      createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "initialMapMode" ), QStringLiteral( "digitize" ) );
    }

    // Insure the layer is ready cloud-friendly
    tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/cloud_action" ), QStringLiteral( "offline" ) );
    tracksLayer->setCustomProperty( QStringLiteral( "QFieldSync/action" ), QStringLiteral( "offline" ) );

    createdProjectLayers << tracksLayer;
  }

  // Basemap
  QgsMapLayer *basemapLayer = nullptr;
  QgsRectangle basemapExtent;
  const QString basemap = options.value( QStringLiteral( "basemap" ), QStringLiteral( "color" ) ).toString();
  const QString basemapCustomProvider = options.value( QStringLiteral( "basemap_custom_provider" ) ).toString();
  const QString basemapCustomExtent = options.value( QStringLiteral( "basemap_custom_extent" ) ).toString();
  QString basemapCustomSource = options.value( QStringLiteral( "basemap_custom_source" ) ).toString();
  if ( basemap.compare( QStringLiteral( "colorful" ) ) == 0 || basemap.compare( QStringLiteral( "darkgray" ) ) == 0 || basemap.compare( QStringLiteral( "lightgray" ) ) == 0 )
  {
    basemapLayer = LayerUtils::createBasemap( basemap );
    if ( basemap.compare( QStringLiteral( "darkgray" ) ) == 0 )
    {
      createdProject->setBackgroundColor( QColor( 15, 15, 15 ) );
    }
    else if ( basemap.compare( QStringLiteral( "lightgray" ) ) == 0 )
    {
      createdProject->setBackgroundColor( QColor( 240, 240, 240 ) );
    }
    else
    {
      createdProject->setBackgroundColor( QColor( 242, 239, 233 ) );
    }
  }
  else if ( basemap.compare( QStringLiteral( "custom" ) ) == 0 || ( !basemapCustomSource.isEmpty() && !basemapCustomProvider.isEmpty() ) )
  {
    if ( basemapCustomProvider.toLower() == QStringLiteral( "vectortile" ) )
    {
      QgsVectorTileUtils::updateUriSources( basemapCustomSource );
      QgsVectorTileLayer *layer = new QgsVectorTileLayer( basemapCustomSource, tr( "Basemap" ) );
      QString error;
      QStringList warnings;
      QList<QgsMapLayer *> subLayers;
      layer->loadDefaultStyleAndSubLayers( error, warnings, subLayers );
      basemapLayer = layer;
    }
    else
    {
      basemapLayer = new QgsRasterLayer( basemapCustomSource, tr( "Basemap" ), basemapCustomProvider );
    }

    basemapExtent = basemapLayer->extent();
    if ( !basemapCustomExtent.isEmpty() )
    {
      const QgsRectangle customExtent = QgsRectangle::fromWkt( basemapCustomExtent );
      if ( !customExtent.isEmpty() )
      {
        basemapExtent = customExtent;
      }
    }
  }

  QgsRectangle createdProjectExtent;
  if ( basemapLayer && basemapLayer->isValid() )
  {
    createdProjectLayers << basemapLayer;
    createdProject->setCrs( basemapLayer->crs() );
    createdProjectExtent = basemapExtent;
  }

  // Insure attachment directories are populated in preparation for cloud project
  QStringList attachmentDirectories = QStringList() << "DCIM"
                                                    << "audio"
                                                    << "video"
                                                    << "files";
  if ( sungsanFieldTemplate )
    attachmentDirectories << "photos";
  createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "attachmentDirs" ), attachmentDirectories );

  createdProject->addMapLayers( createdProjectLayers );

  if ( sungsanFieldObjectsLayer && sungsanFieldPhotosLayer )
  {
    QgsRelation photoRelation { QgsRelationContext( createdProject ) };
    photoRelation.setId( QStringLiteral( "sungsan_field_photos" ) );
    photoRelation.setName( QStringLiteral( "현장 사진" ) );
    photoRelation.setReferencedLayer( sungsanFieldObjectsLayer->id() );
    photoRelation.setReferencingLayer( sungsanFieldPhotosLayer->id() );
    photoRelation.addFieldPair( QStringLiteral( "object_id" ), QStringLiteral( "object_id" ) );
    photoRelation.setStrength( Qgis::RelationshipStrength::Composition );
    photoRelation.updateRelationStatus();
    if ( !photoRelation.isValid() )
    {
      createdProject->clear();
      delete createdProject;
      return QString();
    }
    createdProject->relationManager()->addRelation( photoRelation );
  }

  // Register the notes
  if ( notesPointLayer && attachmentsLayer )
  {
    QgsRelationContext relationContext( createdProject );
    QgsPolymorphicRelation relation( relationContext );
    relation.setId( QStringLiteral( "notes_attachments_relation" ) );
    relation.setName( tr( "Attachments" ) );
    relation.setReferencingLayer( attachmentsLayer->id() );
    QStringList referencedLayerIds;
    referencedLayerIds << notesPointLayer->id();
    if ( notesLineLayer )
    {
      referencedLayerIds << notesLineLayer->id();
    }
    if ( notesPolygonLayer )
    {
      referencedLayerIds << notesPolygonLayer->id();
    }
    relation.setReferencedLayerIds( referencedLayerIds );
    relation.setReferencedLayerExpression( QStringLiteral( "@layer_id" ) );
    relation.setReferencedLayerField( QStringLiteral( "note_layer" ) );
    relation.addFieldPair( QStringLiteral( "note_uuid" ), QStringLiteral( "uuid" ) );
    relation.setRelationStrength( Qgis::RelationshipStrength::Association );
    if ( relation.isValid() )
    {
      createdProject->relationManager()->addPolymorphicRelation( relation );
    }
  }

  if ( options.value( QStringLiteral( "auto_push_to_cloud" ) ).toBool() )
  {
    createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "forceAutoPush" ), true );
    createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "forceAutoPushIntervalMins" ), 30 );
  }

  connect( createdProject, &QgsProject::writeProject, [createdProject, createdProjectExtent, positionInformation]( QDomDocument &document ) {
    QDomNodeList nodes = document.elementsByTagName( "qgis" );
    if ( !nodes.isEmpty() )
    {
      QDomNode node = nodes.item( 0 );
      QDomElement element = node.toElement();

      QDomElement canvasElement = document.createElement( QStringLiteral( "mapcanvas" ) );
      canvasElement.setAttribute( QStringLiteral( "name" ), QStringLiteral( "theMapCanvas" ) );

      node.appendChild( canvasElement );

      QgsRectangle extent = PositioningUtils::createExtentForDevice( positionInformation, createdProject->crs(), createdProjectExtent );
      if ( !extent.isEmpty() )
      {
        QgsMapSettings mapSettings;
        mapSettings.setDestinationCrs( createdProject->crs() );
        mapSettings.setOutputSize( QSize( 500, 500 ) );
        mapSettings.setExtent( extent );
        mapSettings.writeXml( canvasElement, document );
      }
    }
  } );

  const bool written = createdProject->write( projectFilepath );
  createdProject->clear();
  createdProject->deleteLater();

  // Remove any pre-existing settings
  QSettings().remove( QStringLiteral( "/qgis/projectInfo/%1" ).arg( projectFilepath ) );

  return written ? projectFilepath : QString();
}
