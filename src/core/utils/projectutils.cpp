// Modified for Meta Engineering GIS by Sungsan on 2026-08-07.
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
#include "fileutils.h"
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
#include <qgsvectortilelayer.h>
#include <qgsvectortileutils.h>
#include <qgsvectorlayer.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QTextStream>


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
    layer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/vworldSatellite" ), true );
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

bool ProjectUtils::exportFieldSurveyComparisonReport( QgsProject *project, const QString &projectDirectory )
{
  if ( !project || projectDirectory.trimmed().isEmpty() )
  {
    return false;
  }

  QString objectLayerId;
  QString photoLayerId;
  QStringList sourceTextFiles;
  const auto layers = project->mapLayers();

  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    auto *layer = qobject_cast<QgsVectorLayer *>( it.value() );
    if ( !layer || !layer->isValid() )
    {
      continue;
    }
    if ( layer->customProperty( QStringLiteral( "kr.co.metaengi.mobilegis/landstarImportTarget" ) ).toBool()
         || layer->customProperty( QStringLiteral( "kr.co.metaengi.mobilegis/fieldObjects" ) ).toBool()
         || layer->name() == QStringLiteral( "메타이엔지_현장객체" ) )
    {
      objectLayerId = it.key();
      continue;
    }
    if ( layer->customProperty( QStringLiteral( "kr.co.metaengi.mobilegis/fieldPhotos" ) ).toBool()
         || layer->name() == QStringLiteral( "메타이엔지_현장사진" ) )
    {
      photoLayerId = it.key();
    }
  }

  if ( objectLayerId.isEmpty() )
  {
    return false;
  }

  auto *objectLayer = qobject_cast<QgsVectorLayer *>( layers.value( objectLayerId ) );
  if ( !objectLayer )
  {
    return false;
  }

  auto normalizeRelativePath = [&]( const QString &path ) -> QString
  {
    const QString trimmedPath = path.trimmed();
    if ( trimmedPath.isEmpty() )
    {
      return QString();
    }

    QString candidate = trimmedPath;
    if ( candidate.startsWith( QStringLiteral( "file://" ) ) )
    {
      candidate = QUrl( candidate ).toLocalFile();
    }
    const QFileInfo candidateInfo( candidate );
    if ( candidateInfo.isAbsolute() )
    {
      return candidateInfo.absoluteFilePath();
    }

    if ( candidate.contains( QLatin1String( "/" ) ) || candidate.contains( QLatin1String( "\\" ) ) )
    {
      return QDir( projectDirectory ).filePath( candidate );
    }
    return QDir( projectDirectory ).filePath( QStringLiteral( "LandStar/원본" ) + QDir::separator() + candidate );
  };

  auto sourceTextFilter = []( const QString &filePath ) -> bool
  {
    const QString suffix = QFileInfo( filePath ).suffix().toLower();
    return suffix == QStringLiteral( "txt" )
           || suffix == QStringLiteral( "csv" )
           || suffix == QStringLiteral( "pxy" )
           || suffix == QStringLiteral( "kof" );
  };

  auto quoteCsv = []( const QString &value ) -> QString
  {
    QString escaped = value;
    if ( escaped.contains( '\n' ) || escaped.contains( '\r' ) || escaped.contains( '\"' ) || escaped.contains( ',' ) )
    {
      escaped.replace( QStringLiteral( "\"" ), QStringLiteral( "\"\"" ) );
      return QStringLiteral( "\"%1\"" ).arg( escaped );
    }
    escaped.replace( QStringLiteral( "\"" ), QStringLiteral( "\"\"" ) );
    return QStringLiteral( "\"%1\"" ).arg( escaped );
  };

  const QString projectTitle = project->title();
  const QString regionName = project->readEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/regionName" ), QString() );
  const QString siteName = project->readEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/siteName" ), QString() );
  const QString workDate = project->readEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/workDate" ), QString() );
  const QString objectLayerName = objectLayerId.isEmpty() ? QString() : layers.value( objectLayerId )->name();
  const QString reportPath = QStringLiteral( "%1/survey_compare_%2.txt" ).arg( projectDirectory, QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) ) );

  const QString reportHeader = QStringLiteral( "project_title,region_name,site_name,work_date,layer_name,object_id,landstar_id,landstar_code,easting,northing,elevation,name,category,color,fix_status,gps_accuracy_m,surveyed_at,source_device,source_text_file,photo_count,photo_list\n" );
  QString reportBody;
  QHash<QString, QStringList> photoByObject;

  if ( !photoLayerId.isEmpty() )
  {
    auto *photoLayer = qobject_cast<QgsVectorLayer *>( layers.value( photoLayerId ) );
    if ( photoLayer )
    {
      const QgsFields photoFields = photoLayer->fields();
      const int objectIdIdx = photoFields.indexOf( QStringLiteral( "object_id" ) );
      const int mediaIdx = photoFields.indexOf( QStringLiteral( "media" ) );
      const int photoTypeIdx = photoFields.indexOf( QStringLiteral( "photo_type" ) );
      const int seqIdx = photoFields.indexOf( QStringLiteral( "sequence" ) );

      if ( objectIdIdx >= 0 )
      {
        QgsFeatureIterator photoIterator = photoLayer->getFeatures();
        QgsFeature photoFeature;
        while ( photoIterator.nextFeature( photoFeature ) )
        {
          const QString objectId = photoFeature.attribute( objectIdIdx ).toString().trimmed();
          if ( objectId.isEmpty() )
          {
            continue;
          }
          const QString media = mediaIdx >= 0 ? photoFeature.attribute( mediaIdx ).toString().trimmed() : QString();
          const QString type = photoTypeIdx >= 0 ? photoFeature.attribute( photoTypeIdx ).toString().trimmed() : QString();
          const QString seqRaw = seqIdx >= 0 ? photoFeature.attribute( seqIdx ).toString().trimmed() : QString();
          const QString typeNormalized = type.isEmpty() ? QStringLiteral( "기타" ) : type;
          const QString seqLabel = seqRaw.isEmpty() ? QStringLiteral( "1" ) : seqRaw;
          if ( !media.isEmpty() )
          {
            photoByObject[objectId].append( QStringLiteral( "%1|%2|%3" ).arg( typeNormalized, seqLabel, media ) );
          }
        }
      }
    }
  }

  const auto gatherLandstarSources = [&]( const QString &fileOrPath )
  {
    if ( fileOrPath.isEmpty() )
    {
      return;
    }
    const QString normalized = normalizeRelativePath( fileOrPath );
    if ( !normalized.isEmpty() && QFileInfo::exists( normalized ) && sourceTextFilter( normalized ) )
    {
      sourceTextFiles << normalized;
    }
  };

  const auto objectLayerFields = objectLayer->fields();
  const int sourceIdx = objectLayerFields.indexOf( QStringLiteral( "source_file" ) );
  const int landstarIdIdx = objectLayerFields.indexOf( QStringLiteral( "landstar_id" ) );
  const int objectIdIdx = objectLayerFields.indexOf( QStringLiteral( "object_id" ) );
  const int nameIdx = objectLayerFields.indexOf( QStringLiteral( "name" ) );
  const int codeIdx = objectLayerFields.indexOf( QStringLiteral( "landstar_code" ) );
  const int categoryIdx = objectLayerFields.indexOf( QStringLiteral( "category" ) );
  const int colorIdx = objectLayerFields.indexOf( QStringLiteral( "color" ) );
  const int nIdx = objectLayerFields.indexOf( QStringLiteral( "northing" ) );
  const int eIdx = objectLayerFields.indexOf( QStringLiteral( "easting" ) );
  const int zIdx = objectLayerFields.indexOf( QStringLiteral( "elevation" ) );
  const int fixIdx = objectLayerFields.indexOf( QStringLiteral( "fix_status" ) );
  const int accIdx = objectLayerFields.indexOf( QStringLiteral( "gps_accuracy_m" ) );
  const int surveyedAtIdx = objectLayerFields.indexOf( QStringLiteral( "surveyed_at" ) );
  const int sourceDeviceIdx = objectLayerFields.indexOf( QStringLiteral( "source_device" ) );
  const QList<QPair<QString, QString>> fixedPhotoFields = {
    { QStringLiteral( "photo_near" ), QStringLiteral( "근경" ) },
    { QStringLiteral( "photo_far" ), QStringLiteral( "원경" ) },
    { QStringLiteral( "photo_other" ), QStringLiteral( "기타" ) },
    { QStringLiteral( "photo_other_2" ), QStringLiteral( "기타" ) },
  };

  QgsFeatureIterator featureIterator = objectLayer->getFeatures();
  QgsFeature feature;
  while ( featureIterator.nextFeature( feature ) )
  {
    const QString landstarId = landstarIdIdx >= 0 ? feature.attribute( landstarIdIdx ).toString().trimmed() : QString();
    const QString objectId = objectIdIdx >= 0 ? feature.attribute( objectIdIdx ).toString().trimmed() : QString();
    const QString pointName = nameIdx >= 0 ? feature.attribute( nameIdx ).toString().trimmed() : QString();
    const QString code = codeIdx >= 0 ? feature.attribute( codeIdx ).toString().trimmed() : QString();
    const QString category = categoryIdx >= 0 ? feature.attribute( categoryIdx ).toString().trimmed() : QString();
    const QString color = colorIdx >= 0 ? feature.attribute( colorIdx ).toString().trimmed() : QString();
    const QString fixStatus = fixIdx >= 0 ? feature.attribute( fixIdx ).toString().trimmed() : QString();
    const QString accuracy = accIdx >= 0 ? feature.attribute( accIdx ).toString().trimmed() : QString();
    const QString surveyedAt = surveyedAtIdx >= 0 && !feature.attribute( surveyedAtIdx ).isNull() ? feature.attribute( surveyedAtIdx ).toDateTime().toString( QStringLiteral( "yyyy-MM-dd HH:mm:ss" ) ) : QString();
    const QString sourceDevice = sourceDeviceIdx >= 0 ? feature.attribute( sourceDeviceIdx ).toString().trimmed() : QString();
    const QString sourceFile = sourceIdx >= 0 ? feature.attribute( sourceIdx ).toString().trimmed() : QString();
    gatherLandstarSources( sourceFile );

    QString pointN = nIdx >= 0 ? feature.attribute( nIdx ).toString().trimmed() : QString();
    QString pointE = eIdx >= 0 ? feature.attribute( eIdx ).toString().trimmed() : QString();
    QString pointZ = zIdx >= 0 ? feature.attribute( zIdx ).toString().trimmed() : QString();
    if ( pointN.isEmpty() || pointE.isEmpty() )
    {
      const QgsGeometry geometry = feature.geometry();
      if ( !geometry.isNull() )
      {
        const QgsPointXY point2d = geometry.asPoint();
        pointE = point2d.isEmpty() ? QString() : QString::number( point2d.x(), 'f', 4 );
        pointN = point2d.isEmpty() ? QString() : QString::number( point2d.y(), 'f', 4 );
      }
    }

    const QString objectKey = objectId.isEmpty() ? landstarId : objectId;
    QStringList photos = photoByObject.value( objectKey );
    int fixedPhotoSequence = 1;
    for ( const auto &fixedPhotoField : fixedPhotoFields )
    {
      const int photoFieldIndex = objectLayerFields.indexOf( fixedPhotoField.first );
      const QString media = photoFieldIndex >= 0 ? feature.attribute( photoFieldIndex ).toString().trimmed() : QString();
      if ( !media.isEmpty() )
      {
        photos.append( QStringLiteral( "%1|%2|%3" ).arg( fixedPhotoField.second, QString::number( fixedPhotoSequence ), media ) );
      }
      fixedPhotoSequence += 1;
    }
    QStringList photoInfo;
    photoInfo.reserve( photos.size() );
    for ( const QString &photo : photos )
    {
      const QStringList fields = photo.split( QStringLiteral( "|" ) );
      const QString type = fields.value( 0 );
      const QString seq = fields.value( 1 );
      const QString media = fields.value( 2 );
      photoInfo.append( QStringLiteral( "%1#%2=%3" ).arg( type, seq, media ) );
    }

    reportBody += QStringLiteral( "%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18,%19,%20,%21\n" )
      .arg( quoteCsv( projectTitle ),
            quoteCsv( regionName ),
            quoteCsv( siteName ),
            quoteCsv( workDate ),
            quoteCsv( objectLayerName ),
            quoteCsv( objectKey ),
            quoteCsv( landstarId ),
            quoteCsv( code ),
            quoteCsv( pointE ),
            quoteCsv( pointN ),
            quoteCsv( pointZ ),
            quoteCsv( pointName ),
            quoteCsv( category ),
            quoteCsv( color ),
            quoteCsv( fixStatus ),
            quoteCsv( accuracy ),
            quoteCsv( surveyedAt ),
            quoteCsv( sourceDevice ),
            quoteCsv( sourceFile ),
            quoteCsv( QString::number( photos.size() ) ),
            quoteCsv( photoInfo.join( QStringLiteral( "|" ) ) ) );
  }

  if ( sourceTextFiles.isEmpty() )
  {
    const QDir sourceDir = QDir( QStringLiteral( "%1/LandStar/원본" ).arg( projectDirectory ) );
    const QFileInfoList sourceCandidates = sourceDir.entryInfoList( QStringList() << "*.txt" << "*.pxy" << "*.kof" << "*.csv", QDir::Files | QDir::Readable );
    for ( const QFileInfo &sourceFile : sourceCandidates )
    {
      sourceTextFiles << sourceFile.fileName();
    }
  }

  sourceTextFiles.removeDuplicates();
  sourceTextFiles.sort();

  const QString reportComment = QStringLiteral( "# project=%1\n# layer=%2\n# exported=%3\n" )
                                  .arg( projectTitle.isEmpty() ? QObject::tr( "Unnamed" ) : projectTitle )
                                  .arg( objectLayerName.isEmpty() ? QStringLiteral( "N/A" ) : objectLayerName )
                                  .arg( QDateTime::currentDateTime().toString( Qt::ISODate ) );
  QString reportContent = QStringLiteral( "\ufeff" ) + reportComment + reportHeader + reportBody;
  if ( !sourceTextFiles.isEmpty() )
  {
    QStringList sourceLines;
    sourceLines << QStringLiteral( "# source_text_files" );
    for ( const QString &sourceFile : sourceTextFiles )
    {
      sourceLines << QStringLiteral( "# source=%1" ).arg( sourceFile );
    }
    sourceLines << QStringLiteral( "# ---\n" );
    reportContent.append( sourceLines.join( QStringLiteral( "\n" ) ) );
  }
  return FileUtils::writeFileContent( reportPath, reportContent.toUtf8() );
}

QString ProjectUtils::createProject( const QVariantMap &options, const GnssPositionInformation &positionInformation )
{
  const bool sungsanFieldTemplate = options.value( QStringLiteral( "sungsan_field_template" ) ).toBool();
  QString projectTitle = options.value( QStringLiteral( "title" ), tr( "Created Project" ) ).toString();
  QString regionName = options.value( QStringLiteral( "region_name" ), QString() ).toString();
  QString siteName = options.value( QStringLiteral( "site_name" ), QString() ).toString();
  QString workDate = options.value( QStringLiteral( "work_date" ), QString() ).toString();
  QString projectFilename = FileUtils::sanitizeFilePathPart( projectTitle );
  if ( projectFilename.isEmpty() )
  {
    projectFilename = tr( "Created_Project" );
  }

  const QString safeRegion = FileUtils::sanitizeFilePathPart( regionName.trimmed().isEmpty() ? QStringLiteral( "지역" ) : regionName.trimmed() );
  const QString safeSite = FileUtils::sanitizeFilePathPart( siteName.trimmed().isEmpty() ? projectFilename : siteName.trimmed() );
  QString safeDate = workDate.trimmed();
  const QRegularExpression validDate( QStringLiteral( "^\\d{8}$" ) );
  if ( safeDate.isEmpty() || !validDate.match( safeDate ).hasMatch() )
  {
    const QDate today = QDate::currentDate();
    safeDate = QStringLiteral( "%1%2%3" ).arg( today.year(), 4, 10, QLatin1Char( '0' ) ).arg( today.month(), 2, 10, QLatin1Char( '0' ) ).arg( today.day(), 2, 10, QLatin1Char( '0' ) );
  }

  QDir createdProjectsDir( QStringLiteral( "%1/Created Projects/" ).arg( PlatformUtilities::instance()->applicationDirectory() ) );
  QString createdProjectDir = createdProjectsDir.filePath( QStringLiteral( "%1/%2/%3" ).arg( safeRegion, safeSite, QStringLiteral( "%1_%2" ).arg( safeDate, safeSite ) ) );
  int uniqueSuffix = 2;
  while ( QFileInfo::exists( createdProjectDir ) )
  {
    createdProjectDir = QStringLiteral( "%1_%2" ).arg( createdProjectsDir.filePath( QStringLiteral( "%1/%2/%3" ).arg( safeRegion, safeSite, QStringLiteral( "%1_%2" ).arg( safeDate, safeSite ) ) ), QString::number( uniqueSuffix++ ) );
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
  createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/regionName" ), regionName );
  createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/siteName" ), siteName );
  createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/workDate" ), safeDate );

  // Basic project settings
  const QgsCoordinateReferenceSystem defaultProjectCrs( QStringLiteral( "EPSG:3857" ) );
  createdProject->setCrs( defaultProjectCrs );
  createdProject->displaySettings()->setCoordinateType( Qgis::CoordinateDisplayType::CustomCrs );
  createdProject->displaySettings()->setCoordinateCustomCrs( QgsCoordinateReferenceSystem( "EPSG:4326" ) );

  if ( sungsanFieldTemplate )
  {
    createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/fieldPackage" ), QStringLiteral( "kr.co.metaengi.mobilegis.field-package/1" ) );
    createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/template" ), QStringLiteral( "field-landstar" ) );
    // A newly created generic project cannot know the coordinate system used
    // by a LandStar job.  Projected N/E imports stay locked until a desktop
    // field package records an explicit CRS confirmation.
    createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/landstarCrsConfirmed" ), false );
    createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/landstarCrsAuthId" ), defaultProjectCrs.authid() );
    createdProject->writeEntry( QStringLiteral( "MetaEngiMobileGIS" ), QStringLiteral( "/landstarCrsNotice" ), QStringLiteral( "LandStar 좌표계는 현재 프로젝트 좌표계와 일치해야 합니다. 다르면 QGIS에서 확인 후 변환하세요." ) );
    createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "initialMapMode" ), QStringLiteral( "digitize" ) );
  }

  QgsVectorLayer *sungsanFieldObjectsLayer = nullptr;

  if ( sungsanFieldTemplate )
  {
    const QString fieldFilepath = QStringLiteral( "%1/metaengi_field.gpkg" ).arg( createdProjectDir );

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
    // Fixed field-photo slots keep capture simple in the field: no child-row
    // creation is required and every photo has a deterministic file name.
    objectFields.append( QgsField( QStringLiteral( "photo_near" ), QMetaType::QString, QString(), 500 ) );
    objectFields.append( QgsField( QStringLiteral( "photo_far" ), QMetaType::QString, QString(), 500 ) );
    objectFields.append( QgsField( QStringLiteral( "photo_other" ), QMetaType::QString, QString(), 500 ) );
    objectFields.append( QgsField( QStringLiteral( "photo_other_2" ), QMetaType::QString, QString(), 500 ) );

    QgsVectorFileWriter::SaveVectorOptions objectWriterOptions;
    objectWriterOptions.driverName = QStringLiteral( "GPKG" );
    objectWriterOptions.layerName = QStringLiteral( "metaengi_field_objects" );
    QgsVectorFileWriter *objectWriter = QgsVectorFileWriter::create( fieldFilepath, objectFields, Qgis::WkbType::PointZ, defaultProjectCrs, createdProject->transformContext(), objectWriterOptions );
    delete objectWriter;

    const QString objectUri = QStringLiteral( "%1|layername=%2" ).arg( fieldFilepath, objectWriterOptions.layerName );
    sungsanFieldObjectsLayer = new QgsVectorLayer( objectUri, QStringLiteral( "메타이엔지_현장객체" ) );
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
      { QStringLiteral( "photo_other_2" ), QStringLiteral( "기타2 사진" ) },
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
    sungsanFieldObjectsLayer->setDefaultValueDefinition( sourceDeviceIndex, QgsDefaultValue( QStringLiteral( "'메타이엔지 GIS'" ), false ) );

    QVariantMap emptyWidgetOptions;
    const QStringList hiddenObjectFields = { QStringLiteral( "fid" ), QStringLiteral( "object_id" ) };
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

    QVariantMap fieldPhotoOptions;
    fieldPhotoOptions.insert( QStringLiteral( "DocumentViewer" ), 1 );
    fieldPhotoOptions.insert( QStringLiteral( "FileWidget" ), true );
    fieldPhotoOptions.insert( QStringLiteral( "FileWidgetButton" ), true );
    fieldPhotoOptions.insert( QStringLiteral( "RelativeStorage" ), 1 );
    fieldPhotoOptions.insert( QStringLiteral( "StorageMode" ), 0 );
    fieldPhotoOptions.insert( QStringLiteral( "UseLink" ), false );
    const QStringList fixedPhotoFields = {
      QStringLiteral( "photo_near" ), QStringLiteral( "photo_far" ),
      QStringLiteral( "photo_other" ), QStringLiteral( "photo_other_2" ) };
    for ( const QString &fieldName : fixedPhotoFields )
    {
      const int fieldIndex = sungsanFieldObjectsLayer->fields().indexOf( fieldName );
      sungsanFieldObjectsLayer->setEditorWidgetSetup( fieldIndex, QgsEditorWidgetSetup( QStringLiteral( "ExternalResource" ), fieldPhotoOptions ) );
    }

    const QString photoNameBaseExpression = QStringLiteral(
      "with_variable('current_fid', $id, "
      "with_variable('object_name_raw', coalesce(nullif(trim(\"name\"), ''), nullif(trim(\"landstar_id\"), ''), nullif(to_string(\"object_id\"), ''), to_string($id)), "
      "with_variable('object_name_safe', "
      "regexp_replace(replace(@object_name_raw, char(92), '_'), "
      "'[/:*?\"<>|]', '_'), "
      "'images/메타이엔지_현장객체/' || @object_name_safe || "
      "if(aggregate(@layer, 'count', $id, filter := $id != @current_fid AND coalesce(nullif(trim(\"name\"), ''), nullif(trim(\"landstar_id\"), '')) = "
      "coalesce(nullif(trim(attribute(@parent, 'name')), ''), nullif(trim(attribute(@parent, 'landstar_id')), ''))) > 0, "
      "'_' || left(coalesce(nullif(to_string(\"object_id\"), ''), to_string(@current_fid)), 8), '') || ' (%1).{extension}')))" );
    QVariantMap fixedPhotoNaming;
    fixedPhotoNaming.insert( QStringLiteral( "photo_near" ), photoNameBaseExpression.arg( 1 ) );
    fixedPhotoNaming.insert( QStringLiteral( "photo_far" ), photoNameBaseExpression.arg( 2 ) );
    fixedPhotoNaming.insert( QStringLiteral( "photo_other" ), photoNameBaseExpression.arg( 3 ) );
    fixedPhotoNaming.insert( QStringLiteral( "photo_other_2" ), photoNameBaseExpression.arg( 4 ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "QFieldSync/attachment_naming" ), QString::fromUtf8( QJsonDocument::fromVariant( fixedPhotoNaming ).toJson( QJsonDocument::Compact ) ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/saveFieldPhotosToGallery" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/managedFieldPhotos" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/fieldPhotoFields" ), fixedPhotoFields );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/photoObjectNameField" ), QStringLiteral( "name" ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/fieldPhotoFolder" ), QStringLiteral( "images/메타이엔지_현장객체" ) );

    sungsanFieldObjectsLayer->setDisplayExpression( QStringLiteral( "coalesce(nullif(trim(\"landstar_id\"), ''), nullif(trim(\"name\"), ''), \"category\", '현장 객체')" ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/fieldObjects" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/landstarImportTarget" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "kr.co.metaengi.mobilegis/fieldPackage" ), true );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "QFieldSync/cloud_action" ), QStringLiteral( "offline" ) );
    sungsanFieldObjectsLayer->setCustomProperty( QStringLiteral( "QFieldSync/action" ), QStringLiteral( "offline" ) );
    LayerUtils::setDefaultRenderer( sungsanFieldObjectsLayer, nullptr, QString(), QStringLiteral( "color" ) );

    QgsEditFormConfig objectFormConfig = sungsanFieldObjectsLayer->editFormConfig();
    objectFormConfig.clearTabs();
    objectFormConfig.setLayout( Qgis::AttributeFormLayout::DragAndDrop );
    QgsAttributeEditorContainer *objectFormRoot = objectFormConfig.invisibleRootContainer();
    const int nameFieldIndex = sungsanFieldObjectsLayer->fields().indexOf( QStringLiteral( "name" ) );
    objectFormRoot->addChildElement( new QgsAttributeEditorField( QStringLiteral( "name" ), nameFieldIndex, objectFormRoot ) );

    QgsAttributeEditorContainer *photoContainer = new QgsAttributeEditorContainer( QStringLiteral( "현장사진" ), objectFormRoot );
    for ( const QString &fieldName : fixedPhotoFields )
    {
      const int fieldIndex = sungsanFieldObjectsLayer->fields().indexOf( fieldName );
      photoContainer->addChildElement( new QgsAttributeEditorField( fieldName, fieldIndex, photoContainer ) );
    }
    objectFormRoot->addChildElement( photoContainer );

    const QStringList orderedObjectFields = {
      QStringLiteral( "landstar_id" ), QStringLiteral( "landstar_code" ),
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
    sungsanFieldObjectsLayer->setEditFormConfig( objectFormConfig );

    QDir( createdProjectDir ).mkpath( QStringLiteral( "images/메타이엔지_현장객체" ) );

    createdProjectLayers << sungsanFieldObjectsLayer;
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
    attachmentDirectories << "images";
  createdProject->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "attachmentDirs" ), attachmentDirectories );

  createdProject->addMapLayers( createdProjectLayers );

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
