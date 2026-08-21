/***************************************************************************
  sungsansurveybridge.cpp
  -----------------------
  LandStar/CAD point exchange for Sungsan Mobile GIS.
 ***************************************************************************/

#include "sungsansurveybridge.h"

#include <algorithm>
#include <utility>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QStringConverter>
#include <QStringDecoder>

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgsabstractgeometry.h>
#include <qgsexception.h>
#include <qgsfeature.h>
#include <qgsfeaturerequest.h>
#include <qgsgeometry.h>
#include <qgsmaplayer.h>
#include <qgspoint.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>
#include <qgsvertexid.h>
#include <qgswkbtypes.h>

namespace
{
  constexpr qint64 MAX_POINT_FILE_BYTES = 25LL * 1024LL * 1024LL;
  constexpr int MAX_POINT_ROWS = 200000;

  struct PointRecord
  {
    QString name;
    QString code;
    QString fixStatus;
    QString surveyedAt;
    double northing = 0.0;
    double easting = 0.0;
    double elevation = 0.0;
    bool elevationValid = false;
  };

  QString normalized( const QString &value )
  {
    QString result = value.trimmed().toLower();
    result.remove( QRegularExpression( QStringLiteral( "[\\s_\\-()\\[\\].]" ) ) );
    return result;
  }

  QStringList parseCsvLine( const QString &line, const QChar delimiter )
  {
    QStringList fields;
    QString current;
    bool quoted = false;
    for ( int i = 0; i < line.size(); ++i )
    {
      const QChar character = line.at( i );
      if ( character == QLatin1Char( '"' ) )
      {
        if ( quoted && i + 1 < line.size() && line.at( i + 1 ) == QLatin1Char( '"' ) )
        {
          current.append( QLatin1Char( '"' ) );
          ++i;
        }
        else
        {
          quoted = !quoted;
        }
      }
      else if ( character == delimiter && !quoted )
      {
        fields.append( current.trimmed() );
        current.clear();
      }
      else
      {
        current.append( character );
      }
    }
    fields.append( current.trimmed() );
    return fields;
  }

  QStringList splitLine( const QString &line, const QChar delimiter )
  {
    if ( delimiter.isSpace() )
      return line.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
    return parseCsvLine( line, delimiter );
  }

  int findHeader( const QStringList &headers, const QStringList &aliases )
  {
    QSet<QString> wanted;
    for ( const QString &alias : aliases )
      wanted.insert( normalized( alias ) );
    for ( int i = 0; i < headers.size(); ++i )
    {
      if ( wanted.contains( normalized( headers.at( i ) ) ) )
        return i;
    }
    return -1;
  }

  int findField( const QgsVectorLayer *layer, const QStringList &aliases )
  {
    QSet<QString> wanted;
    for ( const QString &alias : aliases )
      wanted.insert( normalized( alias ) );
    const QgsFields fields = layer->fields();
    for ( int i = 0; i < fields.count(); ++i )
    {
      if ( wanted.contains( normalized( fields.at( i ).name() ) ) || wanted.contains( normalized( layer->attributeAlias( i ) ) ) )
        return i;
    }
    return -1;
  }

  QString cleanTextValue( QString value )
  {
    value.replace( QLatin1Char( '\r' ), QLatin1Char( ' ' ) );
    value.replace( QLatin1Char( '\n' ), QLatin1Char( ' ' ) );
    value.replace( QLatin1Char( ',' ), QLatin1Char( ' ' ) );
    return value.trimmed();
  }

  QVariantMap errorResult( const QString &message )
  {
    return {
      { QStringLiteral( "ok" ), false },
      { QStringLiteral( "error" ), message },
      { QStringLiteral( "added" ), 0 },
      { QStringLiteral( "updated" ), 0 },
      { QStringLiteral( "skipped" ), 0 },
    };
  }
}

SungsanSurveyBridge::SungsanSurveyBridge( QObject *parent )
  : QObject( parent )
{}

QgsVectorLayer *SungsanSurveyBridge::selectTargetLayer( QgsProject *project, QgsVectorLayer *preferredLayer ) const
{
  const auto usable = []( QgsVectorLayer *layer ) {
    return layer && layer->isValid() && QgsWkbTypes::geometryType( layer->wkbType() ) == Qgis::GeometryType::Point && layer->supportsEditing();
  };

  if ( usable( preferredLayer ) )
    return preferredLayer;
  if ( !project )
    return nullptr;

  const auto layers = project->mapLayers();
  for ( QgsMapLayer *mapLayer : layers )
  {
    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( mapLayer );
    if ( usable( layer ) && ( layer->customProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldObjects" ), false ).toBool() || layer->name() == QStringLiteral( "성산_현장객체" ) ) )
      return layer;
  }
  for ( QgsMapLayer *mapLayer : layers )
  {
    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( mapLayer );
    if ( usable( layer ) )
      return layer;
  }
  return nullptr;
}

QVariantMap SungsanSurveyBridge::importLandStarFile( const QString &filePath, QgsProject *project, QgsVectorLayer *preferredLayer )
{
  const QFileInfo info( filePath );
  if ( !info.isFile() || info.size() <= 0 )
    return errorResult( tr( "LandStar 측점 파일을 찾지 못했습니다." ) );
  if ( info.size() > MAX_POINT_FILE_BYTES )
    return errorResult( tr( "LandStar 측점 파일이 안전 제한(25 MB)을 초과했습니다." ) );

  QgsVectorLayer *layer = selectTargetLayer( project, preferredLayer );
  if ( !layer )
    return errorResult( tr( "측점을 저장할 편집 가능한 점 레이어가 없습니다." ) );

  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly ) )
    return errorResult( tr( "LandStar 측점 파일을 읽지 못했습니다." ) );
  QByteArray bytes = file.readAll();
  file.close();
  if ( bytes.startsWith( QByteArrayLiteral( "\xEF\xBB\xBF" ) ) )
    bytes.remove( 0, 3 );
  QStringDecoder decoder( QStringConverter::Utf8 );
  const QString text = decoder.decode( bytes );
  if ( decoder.hasError() )
    return errorResult( tr( "LandStar 측점 파일 문자 인코딩이 올바르지 않습니다. Android에서는 UTF-8 또는 Windows-949 파일을 선택해 주세요." ) );
  QStringList lines = text.split( QRegularExpression( QStringLiteral( "\\r?\\n" ) ), Qt::SkipEmptyParts );
  lines.erase( std::remove_if( lines.begin(), lines.end(), []( const QString &line ) {
                 const QString trimmed = line.trimmed();
                 return trimmed.isEmpty() || trimmed.startsWith( QLatin1Char( '#' ) ) || trimmed.startsWith( QStringLiteral( "//" ) );
               } ),
               lines.end() );
  if ( lines.isEmpty() )
    return errorResult( tr( "LandStar 측점 파일에 읽을 행이 없습니다." ) );
  if ( lines.size() > MAX_POINT_ROWS + 1 )
    return errorResult( tr( "LandStar 측점 수가 안전 제한(200,000개)을 초과했습니다." ) );

  const QString firstLine = lines.constFirst();
  QChar delimiter = QLatin1Char( ',' );
  if ( firstLine.count( QLatin1Char( '\t' ) ) > firstLine.count( QLatin1Char( ',' ) ) )
    delimiter = QLatin1Char( '\t' );
  else if ( !firstLine.contains( QLatin1Char( ',' ) ) && !firstLine.contains( QLatin1Char( '\t' ) ) )
    delimiter = QLatin1Char( ' ' );

  const QStringList firstFields = splitLine( firstLine, delimiter );
  bool coordinateNumeric = false;
  if ( firstFields.size() >= 3 )
  {
    bool northOk = false;
    bool eastOk = false;
    firstFields.at( 1 ).toDouble( &northOk );
    firstFields.at( 2 ).toDouble( &eastOk );
    coordinateNumeric = northOk && eastOk;
  }
  const bool hasHeader = !coordinateNumeric;
  const QStringList headers = hasHeader ? firstFields : QStringList();

  int nameColumn = 0;
  int northColumn = 1;
  int eastColumn = 2;
  int elevationColumn = 3;
  int codeColumn = 4;
  int fixColumn = -1;
  int timeColumn = -1;
  if ( hasHeader )
  {
    nameColumn = findHeader( headers, { QStringLiteral( "name" ), QStringLiteral( "point" ), QStringLiteral( "pointname" ), QStringLiteral( "pointid" ), QStringLiteral( "id" ), QStringLiteral( "pt" ), QStringLiteral( "측점" ), QStringLiteral( "점번호" ), QStringLiteral( "측점명" ) } );
    northColumn = findHeader( headers, { QStringLiteral( "n" ), QStringLiteral( "north" ), QStringLiteral( "northing" ), QStringLiteral( "x" ), QStringLiteral( "북" ), QStringLiteral( "위도" ), QStringLiteral( "latitude" ), QStringLiteral( "lat" ) } );
    eastColumn = findHeader( headers, { QStringLiteral( "e" ), QStringLiteral( "east" ), QStringLiteral( "easting" ), QStringLiteral( "y" ), QStringLiteral( "동" ), QStringLiteral( "경도" ), QStringLiteral( "longitude" ), QStringLiteral( "lon" ) } );
    elevationColumn = findHeader( headers, { QStringLiteral( "z" ), QStringLiteral( "elevation" ), QStringLiteral( "elev" ), QStringLiteral( "height" ), QStringLiteral( "h" ), QStringLiteral( "표고" ), QStringLiteral( "고도" ) } );
    codeColumn = findHeader( headers, { QStringLiteral( "code" ), QStringLiteral( "description" ), QStringLiteral( "desc" ), QStringLiteral( "featurecode" ), QStringLiteral( "코드" ), QStringLiteral( "설명" ) } );
    fixColumn = findHeader( headers, { QStringLiteral( "fix" ), QStringLiteral( "solution" ), QStringLiteral( "quality" ), QStringLiteral( "status" ), QStringLiteral( "고정상태" ) } );
    timeColumn = findHeader( headers, { QStringLiteral( "time" ), QStringLiteral( "datetime" ), QStringLiteral( "surveytime" ), QStringLiteral( "측량시간" ) } );
  }
  if ( northColumn < 0 || eastColumn < 0 )
    return errorResult( tr( "측점 파일에서 북ing(N)·동ing(E) 좌표 열을 찾지 못했습니다." ) );

  QList<PointRecord> records;
  const int startRow = hasHeader ? 1 : 0;
  int skipped = 0;
  for ( int row = startRow; row < lines.size(); ++row )
  {
    const QStringList values = splitLine( lines.at( row ), delimiter );
    if ( northColumn >= values.size() || eastColumn >= values.size() )
    {
      ++skipped;
      continue;
    }
    bool northOk = false;
    bool eastOk = false;
    PointRecord record;
    record.northing = values.at( northColumn ).toDouble( &northOk );
    record.easting = values.at( eastColumn ).toDouble( &eastOk );
    if ( !northOk || !eastOk )
    {
      ++skipped;
      continue;
    }
    if ( nameColumn >= 0 && nameColumn < values.size() )
      record.name = values.at( nameColumn ).trimmed();
    if ( record.name.isEmpty() )
      record.name = QStringLiteral( "LS_%1" ).arg( row + 1 );
    if ( elevationColumn >= 0 && elevationColumn < values.size() )
      record.elevation = values.at( elevationColumn ).toDouble( &record.elevationValid );
    if ( codeColumn >= 0 && codeColumn < values.size() )
      record.code = values.at( codeColumn ).trimmed();
    if ( fixColumn >= 0 && fixColumn < values.size() )
      record.fixStatus = values.at( fixColumn ).trimmed();
    if ( timeColumn >= 0 && timeColumn < values.size() )
      record.surveyedAt = values.at( timeColumn ).trimmed();
    records.append( record );
  }
  if ( records.isEmpty() )
    return errorResult( tr( "유효한 LandStar 측점 좌표를 찾지 못했습니다." ) );

  const int idField = findField( layer, { QStringLiteral( "landstar_id" ), QStringLiteral( "point_id" ), QStringLiteral( "point_name" ), QStringLiteral( "object_id" ), QStringLiteral( "name" ), QStringLiteral( "측점명" ), QStringLiteral( "객체명" ) } );
  const int codeField = findField( layer, { QStringLiteral( "landstar_code" ), QStringLiteral( "code" ), QStringLiteral( "description" ), QStringLiteral( "desc" ), QStringLiteral( "category" ), QStringLiteral( "코드" ), QStringLiteral( "종류" ) } );
  const int northField = findField( layer, { QStringLiteral( "northing" ), QStringLiteral( "north" ), QStringLiteral( "n" ), QStringLiteral( "x_coord" ), QStringLiteral( "북ing" ) } );
  const int eastField = findField( layer, { QStringLiteral( "easting" ), QStringLiteral( "east" ), QStringLiteral( "e" ), QStringLiteral( "y_coord" ), QStringLiteral( "동ing" ) } );
  const int elevationField = findField( layer, { QStringLiteral( "elevation" ), QStringLiteral( "elev" ), QStringLiteral( "height" ), QStringLiteral( "z" ), QStringLiteral( "표고" ) } );
  const int fixField = findField( layer, { QStringLiteral( "fix_status" ), QStringLiteral( "fix" ), QStringLiteral( "solution" ), QStringLiteral( "quality" ), QStringLiteral( "고정상태" ) } );
  const int timeField = findField( layer, { QStringLiteral( "surveyed_at" ), QStringLiteral( "survey_time" ), QStringLiteral( "measured_at" ), QStringLiteral( "측량시간" ) } );
  const int sourceField = findField( layer, { QStringLiteral( "source_device" ), QStringLiteral( "source" ), QStringLiteral( "자료출처" ) } );

  QHash<QString, QgsFeatureId> existingByName;
  if ( idField >= 0 )
  {
    QgsFeature existing;
    QgsFeatureRequest request;
    request.setSubsetOfAttributes( { idField } );
    request.setFlags( Qgis::FeatureRequestFlag::NoGeometry );
    QgsFeatureIterator iterator = layer->getFeatures( request );
    while ( iterator.nextFeature( existing ) )
    {
      const QString key = existing.attribute( idField ).toString().trimmed().toCaseFolded();
      if ( !key.isEmpty() )
        existingByName.insert( key, existing.id() );
    }
  }

  bool startedEditing = false;
  if ( !layer->isEditable() )
  {
    if ( !layer->startEditing() )
      return errorResult( tr( "대상 점 레이어의 편집을 시작하지 못했습니다." ) );
    startedEditing = true;
  }

  int added = 0;
  int updated = 0;
  const bool layerHasZ = QgsWkbTypes::hasZ( layer->wkbType() );
  const QgsCoordinateReferenceSystem wgs84( QStringLiteral( "EPSG:4326" ) );
  const bool transformLonLat = !layer->crs().isGeographic() && layer->crs().isValid();
  QgsCoordinateTransform coordinateTransform( wgs84, layer->crs(), project ? project->transformContext() : QgsCoordinateTransformContext() );

  for ( const PointRecord &record : std::as_const( records ) )
  {
    QgsPointXY mapPoint( record.easting, record.northing );
    const bool looksLonLat = qAbs( record.easting ) <= 180.0 && qAbs( record.northing ) <= 90.0;
    if ( looksLonLat && transformLonLat )
    {
      try
      {
        mapPoint = coordinateTransform.transform( mapPoint );
      }
      catch ( QgsCsException & )
      {
        ++skipped;
        continue;
      }
    }

    QgsFeature feature( layer->fields() );
    if ( layerHasZ && record.elevationValid )
      feature.setGeometry( QgsGeometry( new QgsPoint( mapPoint.x(), mapPoint.y(), record.elevation ) ) );
    else
      feature.setGeometry( QgsGeometry::fromPointXY( mapPoint ) );

    const auto setValue = [&feature]( const int index, const QVariant &value ) {
      if ( index >= 0 && value.isValid() )
        feature.setAttribute( index, value );
    };
    setValue( idField, record.name );
    setValue( codeField, record.code );
    setValue( northField, record.northing );
    setValue( eastField, record.easting );
    if ( record.elevationValid )
      setValue( elevationField, record.elevation );
    setValue( fixField, record.fixStatus );
    setValue( timeField, record.surveyedAt.isEmpty() ? QDateTime::currentDateTime() : QVariant( record.surveyedAt ) );
    setValue( sourceField, QStringLiteral( "LandStar" ) );

    const QString key = record.name.trimmed().toCaseFolded();
    const auto existingIt = existingByName.constFind( key );
    if ( idField >= 0 && !key.isEmpty() && existingIt != existingByName.constEnd() )
    {
      const QgsFeatureId featureId = existingIt.value();
      QgsGeometry updatedGeometry = feature.geometry();
      if ( !layer->changeGeometry( featureId, updatedGeometry ) )
      {
        ++skipped;
        continue;
      }
      for ( int fieldIndex = 0; fieldIndex < feature.fields().count(); ++fieldIndex )
      {
        if ( feature.attribute( fieldIndex ).isValid() && !feature.attribute( fieldIndex ).isNull() )
          layer->changeAttributeValue( featureId, fieldIndex, feature.attribute( fieldIndex ) );
      }
      ++updated;
    }
    else if ( layer->addFeature( feature ) )
    {
      ++added;
      if ( idField >= 0 && !key.isEmpty() )
        existingByName.insert( key, feature.id() );
    }
    else
    {
      ++skipped;
    }
  }

  if ( startedEditing && !layer->commitChanges() )
  {
    const QString details = layer->commitErrors().join( QStringLiteral( "; " ) );
    layer->rollBack();
    return errorResult( tr( "LandStar 측점 저장에 실패했습니다: %1" ).arg( details ) );
  }
  layer->updateExtents();
  layer->triggerRepaint();
  if ( project )
    project->setDirty( true );

  return {
    { QStringLiteral( "ok" ), true },
    { QStringLiteral( "error" ), QString() },
    { QStringLiteral( "layerName" ), layer->name() },
    { QStringLiteral( "added" ), added },
    { QStringLiteral( "updated" ), updated },
    { QStringLiteral( "skipped" ), skipped },
    { QStringLiteral( "path" ), filePath },
  };
}

QVariantMap SungsanSurveyBridge::exportCadText( QgsVectorLayer *layer, const QString &projectHomePath )
{
  if ( !layer || !layer->isValid() || QgsWkbTypes::geometryType( layer->wkbType() ) != Qgis::GeometryType::Point )
    return errorResult( tr( "CAD TXT를 만들 점 레이어를 선택해 주세요." ) );
  if ( projectHomePath.trimmed().isEmpty() )
    return errorResult( tr( "프로젝트 폴더를 확인하지 못했습니다." ) );

  QDir root( projectHomePath );
  if ( !root.mkpath( QStringLiteral( "CAD_TXT" ) ) )
    return errorResult( tr( "프로젝트 안에 CAD_TXT 폴더를 만들지 못했습니다." ) );
  const QString fileName = QStringLiteral( "Sungsan_CAD_%1.txt" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyyMMdd_HHmmss" ) ) );
  const QString outputPath = root.filePath( QStringLiteral( "CAD_TXT/%1" ).arg( fileName ) );

  QSaveFile file( outputPath );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    return errorResult( tr( "CAD TXT 출력 파일을 만들지 못했습니다." ) );
  QTextStream stream( &file );
  stream.setEncoding( QStringConverter::Utf8 );

  const int idField = findField( layer, { QStringLiteral( "landstar_id" ), QStringLiteral( "point_id" ), QStringLiteral( "point_name" ), QStringLiteral( "object_id" ), QStringLiteral( "name" ), QStringLiteral( "측점명" ), QStringLiteral( "객체명" ) } );
  const int codeField = findField( layer, { QStringLiteral( "landstar_code" ), QStringLiteral( "code" ), QStringLiteral( "description" ), QStringLiteral( "desc" ), QStringLiteral( "category" ), QStringLiteral( "코드" ), QStringLiteral( "종류" ) } );
  const int elevationField = findField( layer, { QStringLiteral( "elevation" ), QStringLiteral( "elev" ), QStringLiteral( "height" ), QStringLiteral( "z" ), QStringLiteral( "표고" ) } );

  int count = 0;
  QgsFeature feature;
  QgsFeatureIterator iterator = layer->getFeatures();
  while ( iterator.nextFeature( feature ) )
  {
    if ( !feature.hasGeometry() || feature.geometry().isEmpty() )
      continue;
    const QgsPoint point = feature.geometry().constGet()->vertexAt( QgsVertexId() );
    QString pointName = idField >= 0 ? feature.attribute( idField ).toString() : QString();
    if ( pointName.trimmed().isEmpty() )
      pointName = QStringLiteral( "P%1" ).arg( feature.id() );
    const QString code = codeField >= 0 ? feature.attribute( codeField ).toString() : QString();
    double elevation = point.z();
    if ( elevationField >= 0 && !feature.attribute( elevationField ).isNull() )
      elevation = feature.attribute( elevationField ).toDouble();
    if ( qIsNaN( elevation ) )
      elevation = 0.0;
    stream << cleanTextValue( pointName ) << ','
           << QString::number( point.y(), 'f', 3 ) << ','
           << QString::number( point.x(), 'f', 3 ) << ','
           << QString::number( elevation, 'f', 3 ) << ','
           << cleanTextValue( code ) << '\n';
    ++count;
  }
  if ( !file.commit() )
    return errorResult( tr( "CAD TXT 파일을 안전하게 저장하지 못했습니다." ) );

  return {
    { QStringLiteral( "ok" ), true },
    { QStringLiteral( "error" ), QString() },
    { QStringLiteral( "path" ), outputPath },
    { QStringLiteral( "count" ), count },
  };
}
