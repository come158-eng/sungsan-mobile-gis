/***************************************************************************
  sungsansurveybridge.cpp
  -----------------------
  LandStar/CAD point exchange for Sungsan Mobile GIS.
 ***************************************************************************/

#include "sungsansurveybridge.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QTextCodec>
#include <QUuid>
#include <QStringConverter>
#include <QStringDecoder>

#include <qgsattributeeditorcontainer.h>
#include <qgsattributeeditorfield.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgscoordinatetransformcontext.h>
#include <qgsabstractgeometry.h>
#include <qgseditformconfig.h>
#include <qgsexception.h>
#include <qgseditorwidgetsetup.h>
#include <qgsexpression.h>
#include <qgsfeature.h>
#include <qgsfeaturerequest.h>
#include <qgsfield.h>
#include <qgsgeometry.h>
#include <qgsmaplayer.h>
#include <qgspoint.h>
#include <qgsproject.h>
#include <qgsvectordataprovider.h>
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
    double horizontalAccuracy = 0.0;
    bool horizontalAccuracyValid = false;
  };

  struct LandStarMetadata
  {
    QString region;
    QString site;
    QString workDate;
    QString projectName;
    QString source = QStringLiteral( "none" );
  };

  enum class FixQuality
  {
    Fixed,
    Rejected,
    Unverified,
  };

  QString normalized( const QString &value )
  {
    QString result = value.trimmed().toLower();
    result.remove( QRegularExpression( QStringLiteral( "[\\s_\\-()\\[\\].]" ) ) );
    return result;
  }

  bool matchesAnyAlias( const QString &value, const QStringList &aliases )
  {
    const QString candidate = normalized( value );
    for ( const QString &alias : aliases )
    {
      if ( candidate == normalized( alias ) )
        return true;
    }
    return false;
  }

  void setMetadataValue( QString &target, const QString &value )
  {
    const QString cleaned = value.trimmed();
    if ( target.trimmed().isEmpty() && !cleaned.isEmpty() )
      target = cleaned;
  }

  QString normalizeDateToken( const QString &rawValue )
  {
    QString value = rawValue.trimmed();
    static const QRegularExpression datePattern( QStringLiteral( "(\\d{4})[-/.]?(\\d{1,2})[-/.]?(\\d{1,2})" ) );
    const QRegularExpressionMatch match = datePattern.match( value );
    if ( !match.hasMatch() )
      return QString();
    const int year = match.captured( 1 ).toInt();
    const int month = match.captured( 2 ).toInt();
    const int day = match.captured( 3 ).toInt();
    if ( year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 )
      return QString();

    return QStringLiteral( "%1%2%3" )
      .arg( year, 4, 10, QLatin1Char( '0' ) )
      .arg( month, 2, 10, QLatin1Char( '0' ) )
      .arg( day, 2, 10, QLatin1Char( '0' ) );
  }

  void parseMetadataLine( const QString &line, LandStarMetadata &metadata )
  {
    QString entry = line.trimmed();
    if ( entry.isEmpty() )
      return;

    if ( entry.startsWith( QLatin1Char( '#' ) ) )
      entry = entry.mid( 1 ).trimmed();
    else if ( entry.startsWith( QStringLiteral( "//" ) ) )
      entry = entry.mid( 2 ).trimmed();

    if ( entry.isEmpty() )
      return;

    QString key;
    QString value;

    if ( entry.contains( QLatin1Char( ':' ) ) )
    {
      const int idx = entry.indexOf( QLatin1Char( ':' ) );
      key = entry.left( idx );
      value = entry.mid( idx + 1 );
    }
    else if ( entry.contains( QLatin1Char( '=' ) ) )
    {
      const int idx = entry.indexOf( QLatin1Char( '=' ) );
      key = entry.left( idx );
      value = entry.mid( idx + 1 );
    }
    else if ( entry.contains( QLatin1Char( ',' ) ) )
    {
      const int idx = entry.indexOf( QLatin1Char( ',' ) );
      key = entry.left( idx );
      value = entry.mid( idx + 1 );
    }
    else if ( entry.contains( QLatin1Char( '\t' ) ) )
    {
      const int idx = entry.indexOf( QLatin1Char( '\t' ) );
      key = entry.left( idx );
      value = entry.mid( idx + 1 );
    }

    if ( key.isEmpty() || value.isEmpty() )
      return;
    value = value.trimmed();
    if ( value.startsWith( QLatin1Char( '\"' ) ) && value.endsWith( QLatin1Char( '\"' ) ) && value.size() > 1 )
      value = value.mid( 1, value.size() - 2 );

    if ( matchesAnyAlias( key, { QStringLiteral( "작업방명" ), QStringLiteral( "작업방" ), QStringLiteral( "작업명" ), QStringLiteral( "작업명칭" ),
                               QStringLiteral( "project" ), QStringLiteral( "project_name" ), QStringLiteral( "프로젝트명" ), QStringLiteral( "작업대상" ) } ) )
    {
      setMetadataValue( metadata.projectName, value );
      metadata.source = QStringLiteral( "metadata" );
      return;
    }

    if ( matchesAnyAlias( key, { QStringLiteral( "지역" ), QStringLiteral( "지역명" ), QStringLiteral( "도" ), QStringLiteral( "도명" ),
                               QStringLiteral( "province" ), QStringLiteral( "sido" ), QStringLiteral( "region" ), QStringLiteral( "region_name" ) } ) )
    {
      setMetadataValue( metadata.region, value );
      metadata.source = QStringLiteral( "metadata" );
      return;
    }

    if ( matchesAnyAlias( key, { QStringLiteral( "현장" ), QStringLiteral( "현장명" ), QStringLiteral( "site" ), QStringLiteral( "site_name" ), QStringLiteral( "작업장" ), QStringLiteral( "작업현장" ), QStringLiteral( "포인트" ) } ) )
    {
      setMetadataValue( metadata.site, value );
      metadata.source = QStringLiteral( "metadata" );
      return;
    }

    if ( matchesAnyAlias( key, { QStringLiteral( "측량일" ), QStringLiteral( "작업일" ), QStringLiteral( "측량일자" ), QStringLiteral( "측량날짜" ),
                               QStringLiteral( "date" ), QStringLiteral( "work_date" ), QStringLiteral( "workdate" ) } ) )
    {
      const QString normalizedDate = normalizeDateToken( value );
      setMetadataValue( metadata.workDate, normalizedDate );
      metadata.source = QStringLiteral( "metadata" );
      return;
    }
  }

  LandStarMetadata detectLandStarMetadata( const QString &text, const QString &filePath )
  {
    LandStarMetadata metadata;
    const QStringList allLines = text.split( QRegularExpression( QStringLiteral( "\\r?\\n" ) ), Qt::SkipEmptyParts );
    for ( const QString &line : allLines )
    {
      parseMetadataLine( line, metadata );
      if ( !metadata.projectName.isEmpty() && !metadata.region.isEmpty() && !metadata.workDate.isEmpty() )
        break;
    }
    Q_UNUSED( filePath )
    if ( !metadata.region.isEmpty() && !metadata.site.isEmpty() )
      metadata.source = QStringLiteral( "metadata" );
    return metadata;
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

  FixQuality classifyFixQuality( const QString &value )
  {
    const QString quality = normalized( value );
    if ( quality.isEmpty() )
      return FixQuality::Unverified;
    if ( quality == QLatin1String( "0" ) || quality == QLatin1String( "1" )
         || quality == QLatin1String( "2" ) || quality == QLatin1String( "5" )
         || quality.contains( QStringLiteral( "float" ) ) || quality.contains( QStringLiteral( "single" ) )
         || quality.contains( QStringLiteral( "autonomous" ) ) || quality.contains( QStringLiteral( "dgps" ) )
         || quality.contains( QStringLiteral( "nofix" ) ) || quality.contains( QStringLiteral( "invalid" ) )
         || quality.contains( QStringLiteral( "notfixed" ) ) || quality.contains( QStringLiteral( "미고정" ) ) )
      return FixQuality::Rejected;
    if ( quality == QLatin1String( "4" ) || quality.contains( QStringLiteral( "rtkfix" ) )
         || quality.contains( QStringLiteral( "fixed" ) ) || quality == QLatin1String( "fix" )
         || quality.contains( QStringLiteral( "고정" ) ) )
      return FixQuality::Fixed;
    return FixQuality::Unverified;
  }

  QString preserveLandStarSource( const QByteArray &bytes, const QFileInfo &sourceInfo, QgsProject *project, QString *error )
  {
    if ( !project || project->homePath().trimmed().isEmpty() )
      return QString();

    QDir projectDirectory( project->homePath() );
    const QString relativeDirectory = QStringLiteral( "LandStar/원본" );
    if ( !projectDirectory.mkpath( relativeDirectory ) )
    {
      if ( error )
        *error = QObject::tr( "LandStar 원본 보관 폴더를 만들지 못했습니다." );
      return QString();
    }

    QString baseName = sourceInfo.completeBaseName().trimmed();
    baseName.replace( QRegularExpression( QStringLiteral( "[^0-9A-Za-z가-힣._-]+" ) ), QStringLiteral( "_" ) );
    baseName = baseName.left( 80 ).trimmed();
    if ( baseName.isEmpty() )
      baseName = QStringLiteral( "LandStar" );
    QString suffix = sourceInfo.suffix().toLower();
    if ( !QRegularExpression( QStringLiteral( "^[a-z0-9]{1,8}$" ) ).match( suffix ).hasMatch() )
      suffix = QStringLiteral( "txt" );
    const QString digest = QString::fromLatin1( QCryptographicHash::hash( bytes, QCryptographicHash::Sha256 ).toHex().left( 16 ) );
    const QString relativePath = QStringLiteral( "%1/%2_%3.%4" ).arg( relativeDirectory, baseName, digest, suffix );
    const QString absolutePath = projectDirectory.filePath( relativePath );
    if ( !QFileInfo::exists( absolutePath ) )
    {
      QSaveFile auditFile( absolutePath );
      if ( !auditFile.open( QIODevice::WriteOnly ) || auditFile.write( bytes ) != bytes.size() || !auditFile.commit() )
      {
        if ( error )
          *error = QObject::tr( "LandStar 원본 파일을 프로젝트 안에 안전하게 보관하지 못했습니다." );
        return QString();
      }
    }
    return QDir::fromNativeSeparators( relativePath );
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

QVariantMap SungsanSurveyBridge::prepareFieldSurveyLayer( QgsProject *project, QgsVectorLayer *layer )
{
  const auto result = []( const bool ok, const bool prepared, const QString &warning = QString(), const QString &error = QString() ) {
    return QVariantMap{
      { QStringLiteral( "ok" ), ok },
      { QStringLiteral( "prepared" ), prepared },
      { QStringLiteral( "warning" ), warning },
      { QStringLiteral( "error" ), error },
      { QStringLiteral( "fieldsAdded" ), QStringList() },
      { QStringLiteral( "photoFields" ), QStringList() },
      { QStringLiteral( "objectNameField" ), QString() },
      { QStringLiteral( "photoFolder" ), QString() },
    };
  };

  if ( !layer || !layer->isValid() )
    return result( false, false, QString(), tr( "사진 기능을 준비할 유효한 조사 레이어가 없습니다." ) );

  const Qgis::GeometryType geometryType = QgsWkbTypes::geometryType( layer->wkbType() );
  if ( geometryType != Qgis::GeometryType::Point
       && geometryType != Qgis::GeometryType::Line
       && geometryType != Qgis::GeometryType::Polygon )
  {
    return result( true, false, tr( "선택한 레이어는 포인트·라인·폴리곤 레이어가 아니어서 사진 기능만 준비하지 않았습니다." ) );
  }

  if ( layer->readOnly() || !layer->supportsEditing() )
    return result( true, false, tr( "선택한 레이어는 읽기 전용이어서 사진 필드를 준비하지 않았습니다. 조회는 계속할 수 있습니다." ) );

  const auto isStringField = [layer]( const QString &fieldName ) {
    const int fieldIndex = layer->fields().lookupField( fieldName );
    return fieldIndex >= 0 && layer->fields().at( fieldIndex ).type() == QMetaType::QString;
  };

  // Prefer a previously prepared four-field mapping, then reuse the legacy
  // Sungsan fields slot by slot. Short ss_photoN names remain valid for DBF
  // providers with a ten-character field-name limit.
  QStringList photoFields;
  const QByteArray configuredPhotoFieldsJson = layer->customProperty(
    QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoFields" ) ).toString().toUtf8();
  const QJsonDocument configuredPhotoFieldsDocument = QJsonDocument::fromJson( configuredPhotoFieldsJson );
  if ( configuredPhotoFieldsDocument.isArray() && configuredPhotoFieldsDocument.array().size() == 4 )
  {
    QStringList configuredFields;
    for ( const QJsonValue &value : configuredPhotoFieldsDocument.array() )
      configuredFields.append( value.toString() );
    if ( std::all_of( configuredFields.cbegin(), configuredFields.cend(), isStringField ) )
      photoFields = configuredFields;
  }

  const QStringList legacyPhotoFields = {
    QStringLiteral( "photo_near" ),
    QStringLiteral( "photo_far" ),
    QStringLiteral( "photo_other" ),
    QStringLiteral( "photo_other_2" ),
  };
  const QStringList managedPhotoFields = {
    QStringLiteral( "ss_photo1" ),
    QStringLiteral( "ss_photo2" ),
    QStringLiteral( "ss_photo3" ),
    QStringLiteral( "ss_photo4" ),
  };

  if ( photoFields.isEmpty() )
  {
    for ( int slot = 0; slot < managedPhotoFields.size(); ++slot )
    {
      if ( isStringField( legacyPhotoFields.at( slot ) ) )
        photoFields.append( legacyPhotoFields.at( slot ) );
      else
        photoFields.append( managedPhotoFields.at( slot ) );
    }
  }

  QStringList missingFields;
  for ( const QString &fieldName : std::as_const( photoFields ) )
  {
    if ( !isStringField( fieldName ) )
      missingFields.append( fieldName );
  }

  QStringList fieldsAdded;
  if ( !missingFields.isEmpty() )
  {
    // Never commit or roll back a session owned by the operator. QField can
    // call this method again after that edit has been saved.
    if ( layer->isEditable() )
    {
      QVariantMap warningResult = result( true, false, tr( "레이어에 저장되지 않은 편집이 있어 사진 필드를 추가하지 않았습니다. 현재 편집을 저장한 뒤 다시 시도해 주세요. 조사는 계속할 수 있습니다." ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      return warningResult;
    }

    QgsVectorDataProvider *provider = layer->dataProvider();
    const QString providerType = layer->providerType().toLower();
    const bool localSchemaProvider = providerType == QLatin1String( "ogr" )
                                     || providerType == QLatin1String( "spatialite" );
    if ( !localSchemaProvider )
    {
      QVariantMap warningResult = result( true, false, tr( "공유·원격 데이터베이스의 스키마는 앱이 자동으로 바꾸지 않습니다. QGIS에서 사진 필드 4개를 준비하면 속성 조사는 그대로 계속할 수 있습니다." ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      return warningResult;
    }
    if ( !provider || !( provider->capabilities() & Qgis::VectorProviderCapability::AddAttributes ) )
    {
      QVariantMap warningResult = result( true, false, tr( "이 데이터 형식은 앱에서 사진 필드를 추가할 수 없습니다. QGIS에서 사진 필드를 준비하면 속성 조사는 그대로 계속할 수 있습니다." ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      return warningResult;
    }

    if ( !layer->startEditing() )
    {
      QVariantMap warningResult = result( true, false, tr( "사진 필드 추가를 위한 편집을 시작하지 못했습니다. 기존 데이터는 변경하지 않았으며 조사는 계속할 수 있습니다." ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      return warningResult;
    }

    bool fieldsStaged = true;
    for ( const QString &fieldName : std::as_const( missingFields ) )
    {
      // 254 characters remains portable to DBF-backed shapefiles while being
      // ample for a project-relative attachment path.
      if ( !layer->addAttribute( QgsField( fieldName, QMetaType::QString, QString(), 254 ) ) )
      {
        fieldsStaged = false;
        break;
      }
      fieldsAdded.append( fieldName );
    }

    if ( !fieldsStaged )
    {
      layer->rollBack();
      layer->updateFields();
      QVariantMap warningResult = result( true, false, tr( "사진 필드를 모두 추가하지 못해 이번 변경을 취소했습니다. 기존 데이터는 유지되며 조사는 계속할 수 있습니다." ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      return warningResult;
    }

    if ( !layer->commitChanges() )
    {
      const QString details = layer->commitErrors().join( QStringLiteral( "; " ) );
      layer->rollBack();
      layer->updateFields();
      QVariantMap warningResult = result(
        true, false,
        details.isEmpty()
          ? tr( "사진 필드를 저장하지 못해 변경을 취소했습니다. 기존 데이터는 유지되며 조사는 계속할 수 있습니다." )
          : tr( "사진 필드를 저장하지 못해 변경을 취소했습니다: %1" ).arg( details ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      return warningResult;
    }
    layer->updateFields();
  }

  for ( const QString &fieldName : std::as_const( photoFields ) )
  {
    if ( !isStringField( fieldName ) )
    {
      QVariantMap warningResult = result( true, false, tr( "사진 필드 구성을 확인하지 못했습니다. 기존 데이터는 유지되며 조사는 계속할 수 있습니다." ) );
      warningResult[QStringLiteral( "photoFields" )] = photoFields;
      warningResult[QStringLiteral( "fieldsAdded" )] = fieldsAdded;
      return warningResult;
    }
  }

  // A desktop-prepared choice wins. Otherwise prefer common Korean GIS asset
  // identifiers before generic name/id fields.
  QString objectNameField = layer->customProperty(
    QStringLiteral( "kr.co.sungsan.mobilegis/photoObjectNameField" ) ).toString().trimmed();
  if ( objectNameField.isEmpty() || layer->fields().lookupField( objectNameField ) < 0 )
  {
    objectNameField.clear();
    const QStringList objectNameAliases = {
      QStringLiteral( "관리번호" ), QStringLiteral( "시설물관리번호" ), QStringLiteral( "시설물번호" ),
      QStringLiteral( "mng_num" ), QStringLiteral( "mng_no" ), QStringLiteral( "manage_no" ),
      QStringLiteral( "management_no" ), QStringLiteral( "asset_no" ), QStringLiteral( "asset_id" ),
      QStringLiteral( "facility_no" ), QStringLiteral( "facility_id" ), QStringLiteral( "객체명" ),
      QStringLiteral( "측점명" ), QStringLiteral( "point_name" ), QStringLiteral( "object_name" ),
      QStringLiteral( "obj_name" ), QStringLiteral( "landstar_id" ), QStringLiteral( "name" ),
      QStringLiteral( "no" ), QStringLiteral( "code" ), QStringLiteral( "object_id" ),
      QStringLiteral( "uuid" ), QStringLiteral( "id" ), QStringLiteral( "fid" ),
    };

    for ( const QString &alias : objectNameAliases )
    {
      for ( int fieldIndex = 0; fieldIndex < layer->fields().size(); ++fieldIndex )
      {
        const QString fieldName = layer->fields().at( fieldIndex ).name();
        if ( normalized( fieldName ) == normalized( alias )
             || normalized( layer->attributeDisplayName( fieldIndex ) ) == normalized( alias ) )
        {
          objectNameField = fieldName;
          break;
        }
      }
      if ( !objectNameField.isEmpty() )
        break;
    }
  }

  QStringList warnings;
  const auto safeLayerFolderComponent = []( QString name, const QString &layerId ) {
    name = name.trimmed();
    name.replace( QRegularExpression( QStringLiteral( "[\\x00-\\x1f/\\\\:*?\"<>|]+" ) ), QStringLiteral( "_" ) );
    name.replace( QRegularExpression( QStringLiteral( "[. ]+$" ) ), QString() );
    if ( name.isEmpty() || name == QLatin1String( "." ) || name == QLatin1String( ".." ) )
      name = QStringLiteral( "layer_%1" ).arg( layerId.left( 12 ) );
    if ( name.size() > 80 )
    {
      name = name.left( 80 ).trimmed();
      name.replace( QRegularExpression( QStringLiteral( "[. ]+$" ) ), QString() );
    }
    if ( name.isEmpty() )
      name = QStringLiteral( "layer_%1" ).arg( layerId.left( 12 ) );
    return name;
  };

  const QString safeLayerName = safeLayerFolderComponent( layer->name(), layer->id() );
  const QString photoFolderProperty = QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoFolder" );
  const QString configuredPhotoFolder = layer->customProperty( photoFolderProperty ).toString().trimmed();
  QString photoFolder = configuredPhotoFolder;
  if ( photoFolder.isEmpty() )
  {
    bool layerFolderCollision = false;
    const QString defaultPhotoFolder = QStringLiteral( "images/%1" ).arg( safeLayerName );
    if ( project )
    {
      const QMap<QString, QgsMapLayer *> projectLayers = project->mapLayers();
      for ( QgsMapLayer *candidate : projectLayers )
      {
        QgsVectorLayer *candidateLayer = qobject_cast<QgsVectorLayer *>( candidate );
        if ( !candidateLayer || candidateLayer == layer || candidateLayer->id() == layer->id() )
          continue;

        const QString candidateFolder = candidateLayer->customProperty( photoFolderProperty ).toString().trimmed();
        const QString candidateSafeName = safeLayerFolderComponent( candidateLayer->name(), candidateLayer->id() );
        if ( candidateLayer->name().compare( layer->name(), Qt::CaseInsensitive ) == 0
             || candidateSafeName.compare( safeLayerName, Qt::CaseInsensitive ) == 0
             || ( !candidateFolder.isEmpty() && candidateFolder.compare( defaultPhotoFolder, Qt::CaseInsensitive ) == 0 ) )
        {
          layerFolderCollision = true;
          break;
        }
      }
    }

    if ( layerFolderCollision )
    {
      const QString layerIdSuffix = QString::fromLatin1(
        QCryptographicHash::hash( layer->id().toUtf8(), QCryptographicHash::Sha256 ).toHex().left( 8 ) );
      photoFolder = QStringLiteral( "%1_%2" ).arg( defaultPhotoFolder, layerIdSuffix );
    }
    else
    {
      photoFolder = defaultPhotoFolder;
    }
  }

  if ( !project || project->homePath().trimmed().isEmpty() )
  {
    warnings.append( tr( "프로젝트 저장 위치가 없어 사진 폴더를 미리 만들지 못했습니다. 프로젝트를 저장한 뒤 사진을 촬영해 주세요." ) );
  }
  else if ( !QDir( project->homePath() ).mkpath( photoFolder ) )
  {
    warnings.append( tr( "사진 폴더(%1)를 만들지 못했습니다. 저장 공간 권한과 여유 공간을 확인해 주세요." ).arg( photoFolder ) );
  }

  if ( project )
  {
    QStringList attachmentDirectories = project->readListEntry(
      QStringLiteral( "qfieldsync" ), QStringLiteral( "attachmentDirs" ), QStringList() );
    if ( !attachmentDirectories.contains( QStringLiteral( "images" ) ) )
    {
      attachmentDirectories.append( QStringLiteral( "images" ) );
      project->writeEntry( QStringLiteral( "qfieldsync" ), QStringLiteral( "attachmentDirs" ), attachmentDirectories );
    }
  }

  QVariantMap photoWidgetOptions;
  photoWidgetOptions.insert( QStringLiteral( "DocumentViewer" ), 1 );
  photoWidgetOptions.insert( QStringLiteral( "FileWidget" ), true );
  photoWidgetOptions.insert( QStringLiteral( "FileWidgetButton" ), true );
  photoWidgetOptions.insert( QStringLiteral( "RelativeStorage" ), 1 );
  photoWidgetOptions.insert( QStringLiteral( "StorageMode" ), 0 );
  photoWidgetOptions.insert( QStringLiteral( "UseLink" ), false );

  const QStringList photoAliases = {
    tr( "근경" ), tr( "원경" ), tr( "기타" ), tr( "기타2" ),
  };

  QJsonObject attachmentNaming;
  const QByteArray currentNamingJson = layer->customProperty( QStringLiteral( "QFieldSync/attachment_naming" ) ).toString().toUtf8();
  const QJsonDocument currentNamingDocument = QJsonDocument::fromJson( currentNamingJson );
  if ( currentNamingDocument.isObject() )
    attachmentNaming = currentNamingDocument.object();

  const QString escapedPhotoFolder = QString( photoFolder ).replace( QLatin1Char( '\'' ), QStringLiteral( "''" ) );
  QString objectNameExpression;
  QString objectNameSafeExpression;
  QString duplicateNameSuffixExpression = QStringLiteral( "''" );
  if ( !objectNameField.isEmpty() )
  {
    objectNameExpression = QStringLiteral( "nullif(trim(to_string(%1)), '')" )
                             .arg( QgsExpression::quotedColumnRef( objectNameField ) );
    objectNameSafeExpression = QStringLiteral(
      "regexp_replace(replace(coalesce(%1, '객체_' || to_string($id)), char(92), '_'), "
      "'[/:*?\"<>|]', '_')" )
                                 .arg( objectNameExpression );
    duplicateNameSuffixExpression = QStringLiteral(
      "if(%1 IS NOT NULL AND aggregate(@layer, 'count', $id, "
      "filter := $id != @current_fid AND lower(%2) = lower(@object_name_safe)) > 0, "
      "'_' || to_string(@current_fid), '')" )
                                      .arg( objectNameExpression, objectNameSafeExpression );
  }
  else
  {
    objectNameExpression = QStringLiteral( "NULL" );
    objectNameSafeExpression = QStringLiteral(
      "regexp_replace(replace('객체_' || to_string($id), char(92), '_'), '[/:*?\"<>|]', '_')" );
    warnings.append( tr( "객체명 필드를 자동으로 찾지 못해 내부 객체 ID를 사진 파일명에 사용합니다. 레이어 설정에서 객체명 필드를 지정할 수 있습니다." ) );
  }

  const QString photoNameBaseExpression = QStringLiteral(
    "with_variable('current_fid', $id, "
    "with_variable('object_name_safe', %1, "
    "'%2/' || @object_name_safe || %3 || '%4'))" );
  const QStringList fixedPhotoSuffixes = {
    QStringLiteral( " (1).{extension}" ),
    QStringLiteral( " (2).{extension}" ),
    QStringLiteral( " (3).{extension}" ),
    QStringLiteral( " (4).{extension}" ),
  };

  QJsonArray photoFieldsJson;
  for ( int slot = 0; slot < photoFields.size(); ++slot )
  {
    const QString fieldName = photoFields.at( slot );
    const int fieldIndex = layer->fields().lookupField( fieldName );
    layer->setFieldAlias( fieldIndex, photoAliases.at( slot ) );
    layer->setEditorWidgetSetup( fieldIndex, QgsEditorWidgetSetup( QStringLiteral( "ExternalResource" ), photoWidgetOptions ) );
    attachmentNaming.insert(
      fieldName,
      photoNameBaseExpression.arg( objectNameSafeExpression, escapedPhotoFolder, duplicateNameSuffixExpression, fixedPhotoSuffixes.at( slot ) ) );
    photoFieldsJson.append( fieldName );
  }

  // Custom drag-and-drop forms only display elements which are explicitly in
  // their tree. Preserve the operator's complete form and append just the
  // missing photo fields to one "현장사진" container. Auto-generated and UI
  // file forms need no tree changes and will discover the new fields normally.
  QgsEditFormConfig formConfig = layer->editFormConfig();
  if ( formConfig.layout() == Qgis::AttributeFormLayout::DragAndDrop )
  {
    QgsAttributeEditorContainer *formRoot = formConfig.invisibleRootContainer();
    if ( formRoot )
    {
      QSet<QString> fieldsInForm;
      const QList<QgsAttributeEditorElement *> fieldElements = formRoot->findElements( Qgis::AttributeEditorType::Field );
      for ( const QgsAttributeEditorElement *element : fieldElements )
        fieldsInForm.insert( element->name() );

      QgsAttributeEditorContainer *photoContainer = nullptr;
      const QList<QgsAttributeEditorElement *> containerElements = formRoot->findElements( Qgis::AttributeEditorType::Container );
      for ( QgsAttributeEditorElement *element : containerElements )
      {
        if ( element->name() == QLatin1String( "현장사진" ) )
        {
          photoContainer = static_cast<QgsAttributeEditorContainer *>( element );
          break;
        }
      }

      bool formChanged = false;
      for ( const QString &fieldName : std::as_const( photoFields ) )
      {
        if ( fieldsInForm.contains( fieldName ) )
          continue;

        if ( !photoContainer )
        {
          photoContainer = new QgsAttributeEditorContainer( tr( "현장사진" ), formRoot );
          formRoot->addChildElement( photoContainer );
          formChanged = true;
        }

        const int fieldIndex = layer->fields().lookupField( fieldName );
        photoContainer->addChildElement( new QgsAttributeEditorField( fieldName, fieldIndex, photoContainer ) );
        fieldsInForm.insert( fieldName );
        formChanged = true;
      }

      if ( formChanged )
        layer->setEditFormConfig( formConfig );
    }
    else
    {
      warnings.append( tr( "레이어의 사용자 정의 입력 폼을 읽지 못해 사진 필드를 폼에 배치하지 못했습니다. QGIS 레이어 속성에서 폼 구성을 확인해 주세요." ) );
    }
  }

  layer->setCustomProperty( QStringLiteral( "QFieldSync/attachment_naming" ),
                            QString::fromUtf8( QJsonDocument( attachmentNaming ).toJson( QJsonDocument::Compact ) ) );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/managedFieldPhotos" ), true );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoFields" ),
                            QString::fromUtf8( QJsonDocument( photoFieldsJson ).toJson( QJsonDocument::Compact ) ) );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/photoObjectNameField" ), objectNameField );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoFolder" ), photoFolder );

  // Keep non-sensitive identifiers with the portable layer configuration so
  // an exported photo can be traced back to its project, layer and feature
  // identity rule without exposing an operator name, device ID or file path.
  QString trackingProjectId;
  if ( project )
  {
    trackingProjectId = project->readEntry(
      QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/fieldPhotoProjectId" ), QString() ).trimmed();
    if ( trackingProjectId.isEmpty() )
    {
      trackingProjectId = QUuid::createUuid().toString( QUuid::WithoutBraces );
      project->writeEntry(
        QStringLiteral( "SungsanMobileGIS" ), QStringLiteral( "/fieldPhotoProjectId" ), trackingProjectId );
    }
  }

  QString featureIdField;
  const QStringList stableFeatureIdCandidates = {
    QStringLiteral( "object_id" ), QStringLiteral( "uuid" ),
    QStringLiteral( "globalid" ), QStringLiteral( "guid" ),
  };
  for ( const QString &candidate : stableFeatureIdCandidates )
  {
    const int fieldIndex = layer->fields().lookupField( candidate );
    if ( fieldIndex >= 0 )
    {
      featureIdField = layer->fields().at( fieldIndex ).name();
      break;
    }
  }
  if ( featureIdField.isEmpty() && layer->dataProvider() )
  {
    const auto primaryKeyIndexes = layer->dataProvider()->pkAttributeIndexes();
    if ( primaryKeyIndexes.size() == 1 && primaryKeyIndexes.constFirst() >= 0
         && primaryKeyIndexes.constFirst() < layer->fields().size() )
    {
      featureIdField = layer->fields().at( primaryKeyIndexes.constFirst() ).name();
    }
  }

  QString featureIdExpression = QStringLiteral( "to_string($id)" );
  if ( !featureIdField.isEmpty() )
  {
    featureIdExpression = QStringLiteral( "coalesce(nullif(trim(to_string(%1)), ''), to_string($id))" )
                            .arg( QgsExpression::quotedColumnRef( featureIdField ) );
  }

  if ( !trackingProjectId.isEmpty() )
    layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoProjectId" ), trackingProjectId );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoLayerId" ), layer->id() );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoFeatureIdField" ), featureIdField );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoFeatureIdExpression" ), featureIdExpression );
  layer->setCustomProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldPhotoMetadataVersion" ), 1 );
  if ( project )
    project->setDirty( true );

  QVariantMap successResult = result( true, true, warnings.join( QLatin1Char( '\n' ) ) );
  successResult[QStringLiteral( "fieldsAdded" )] = fieldsAdded;
  successResult[QStringLiteral( "photoFields" )] = photoFields;
  successResult[QStringLiteral( "objectNameField" )] = objectNameField;
  successResult[QStringLiteral( "photoFolder" )] = photoFolder;
  successResult[QStringLiteral( "trackingProjectId" )] = trackingProjectId;
  successResult[QStringLiteral( "trackingLayerId" )] = layer->id();
  successResult[QStringLiteral( "trackingFeatureIdExpression" )] = featureIdExpression;
  successResult[QStringLiteral( "layerName" )] = layer->name();
  successResult[QStringLiteral( "layerId" )] = layer->id();
  return successResult;
}

QVariantMap SungsanSurveyBridge::queryLandStarMetadata( const QString &filePath ) const
{
  const QFileInfo info( filePath );
  if ( !info.isFile() || info.size() <= 0 )
    return errorResult( tr( "LandStar 측점 파일을 찾지 못했습니다." ) );
  if ( info.size() > MAX_POINT_FILE_BYTES )
    return errorResult( tr( "LandStar 측점 파일이 안전 제한(25 MB)을 초과했습니다." ) );

  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly ) )
    return errorResult( tr( "LandStar 측점 파일을 읽지 못했습니다." ) );

  const QByteArray bytes = file.readAll();
  file.close();
  if ( bytes.size() > MAX_POINT_FILE_BYTES )
    return errorResult( tr( "LandStar 측점 파일이 안전 제한(25 MB)을 초과했습니다." ) );
  const QByteArray normalized = bytes.startsWith( QByteArrayLiteral( "\xEF\xBB\xBF" ) ) ? bytes.mid( 3 ) : bytes;

  QStringDecoder decoder( QStringConverter::Utf8 );
  QString text = decoder.decode( normalized );
  if ( decoder.hasError() )
  {
    const QTextCodec *fallbackCodec = QTextCodec::codecForName( "CP949" );
    if ( fallbackCodec )
    {
      text = fallbackCodec->toUnicode( normalized );
    }
    else
    {
      text = QString::fromLocal8Bit( normalized );
    }
  }

  const LandStarMetadata metadata = detectLandStarMetadata( text, filePath );
  return QVariantMap{
    { QStringLiteral( "ok" ), true },
    { QStringLiteral( "error" ), QString() },
    { QStringLiteral( "project_region" ), metadata.region },
    { QStringLiteral( "project_site" ), metadata.site },
    { QStringLiteral( "project_name" ), metadata.projectName },
    { QStringLiteral( "work_date" ), metadata.workDate },
    { QStringLiteral( "source" ), metadata.source },
  };
}

QgsVectorLayer *SungsanSurveyBridge::selectTargetLayer( QgsProject *project, QgsVectorLayer *preferredLayer, QString *error ) const
{
  if ( error )
    error->clear();

  const auto usableFieldObjectLayer = []( QgsVectorLayer *layer ) {
    if ( !layer || !layer->isValid() || QgsWkbTypes::geometryType( layer->wkbType() ) != Qgis::GeometryType::Point
         || !QgsWkbTypes::hasZ( layer->wkbType() ) || !layer->supportsEditing() )
      return false;

    const bool markedForFieldObjects = layer->customProperty( QStringLiteral( "kr.co.sungsan.mobilegis/fieldObjects" ), false ).toBool() || layer->name() == QStringLiteral( "성산_현장객체" );
    if ( !markedForFieldObjects )
      return false;

    const QStringList requiredFields = {
      QStringLiteral( "object_id" ), QStringLiteral( "name" ), QStringLiteral( "landstar_id" ),
      QStringLiteral( "landstar_code" ), QStringLiteral( "northing" ), QStringLiteral( "easting" ),
      QStringLiteral( "elevation" ), QStringLiteral( "fix_status" ), QStringLiteral( "gps_accuracy_m" ),
      QStringLiteral( "surveyed_at" ), QStringLiteral( "source_device" ), QStringLiteral( "photo_near" ),
      QStringLiteral( "photo_far" ), QStringLiteral( "photo_other" ),
    };
    return std::all_of( requiredFields.cbegin(), requiredFields.cend(), [layer]( const QString &fieldName ) {
      return findField( layer, { fieldName } ) >= 0;
    } );
  };

  if ( usableFieldObjectLayer( preferredLayer ) )
    return preferredLayer;
  if ( !project )
    return nullptr;

  QList<QgsVectorLayer *> explicitTargets;
  QList<QgsVectorLayer *> compatibleTargets;
  const auto layers = project->mapLayers();
  for ( QgsMapLayer *mapLayer : layers )
  {
    QgsVectorLayer *layer = qobject_cast<QgsVectorLayer *>( mapLayer );
    if ( !usableFieldObjectLayer( layer ) )
      continue;

    compatibleTargets.append( layer );
    if ( layer->customProperty( QStringLiteral( "kr.co.sungsan.mobilegis/landstarImportTarget" ), false ).toBool() )
      explicitTargets.append( layer );
  }

  const QList<QgsVectorLayer *> &candidates = explicitTargets.isEmpty() ? compatibleTargets : explicitTargets;
  if ( candidates.size() == 1 )
    return candidates.constFirst();
  if ( candidates.size() > 1 && error )
  {
    QStringList layerNames;
    layerNames.reserve( candidates.size() );
    for ( const QgsVectorLayer *candidate : candidates )
      layerNames.append( candidate->name() );
    *error = tr( "LandStar 저장 대상 레이어가 여러 개입니다(%1). PC 플러그인에서 대상 레이어를 하나만 지정해 현장 패키지를 다시 만들어 주세요." ).arg( layerNames.join( QStringLiteral( ", " ) ) );
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

  QString targetLayerError;
  QgsVectorLayer *layer = selectTargetLayer( project, preferredLayer, &targetLayerError );
  if ( !layer )
    return errorResult( targetLayerError.isEmpty()
                          ? tr( "측점을 저장할 성산 현장객체 레이어가 없습니다. PC 플러그인에서 현장 패키지를 다시 만들어 주세요." )
                          : targetLayerError );

  QFile file( filePath );
  if ( !file.open( QIODevice::ReadOnly ) )
    return errorResult( tr( "LandStar 측점 파일을 읽지 못했습니다." ) );
  QByteArray bytes = file.readAll();
  file.close();
  QByteArray auditBytes = bytes;
  QFile rawAuditFile( filePath + QStringLiteral( ".source" ) );
  if ( rawAuditFile.exists() && rawAuditFile.size() > 0 && rawAuditFile.size() <= MAX_POINT_FILE_BYTES
       && rawAuditFile.open( QIODevice::ReadOnly ) )
  {
    const QByteArray rawBytes = rawAuditFile.readAll();
    rawAuditFile.close();
    if ( !rawBytes.isEmpty() && rawBytes.size() <= MAX_POINT_FILE_BYTES )
      auditBytes = rawBytes;
  }
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
  int accuracyColumn = -1;
  double accuracyScaleToMeters = 1.0;
  if ( hasHeader )
  {
    nameColumn = findHeader( headers, { QStringLiteral( "name" ), QStringLiteral( "point" ), QStringLiteral( "pointname" ), QStringLiteral( "pointid" ), QStringLiteral( "id" ), QStringLiteral( "pt" ), QStringLiteral( "측점" ), QStringLiteral( "점번호" ), QStringLiteral( "측점명" ) } );
    northColumn = findHeader( headers, { QStringLiteral( "n" ), QStringLiteral( "north" ), QStringLiteral( "northing" ), QStringLiteral( "x" ), QStringLiteral( "북" ), QStringLiteral( "위도" ), QStringLiteral( "latitude" ), QStringLiteral( "lat" ) } );
    eastColumn = findHeader( headers, { QStringLiteral( "e" ), QStringLiteral( "east" ), QStringLiteral( "easting" ), QStringLiteral( "y" ), QStringLiteral( "동" ), QStringLiteral( "경도" ), QStringLiteral( "longitude" ), QStringLiteral( "lon" ) } );
    elevationColumn = findHeader( headers, { QStringLiteral( "z" ), QStringLiteral( "elevation" ), QStringLiteral( "elev" ), QStringLiteral( "height" ), QStringLiteral( "h" ), QStringLiteral( "표고" ), QStringLiteral( "고도" ) } );
    codeColumn = findHeader( headers, { QStringLiteral( "code" ), QStringLiteral( "description" ), QStringLiteral( "desc" ), QStringLiteral( "featurecode" ), QStringLiteral( "코드" ), QStringLiteral( "설명" ) } );
    fixColumn = findHeader( headers, { QStringLiteral( "fix" ), QStringLiteral( "solution" ), QStringLiteral( "quality" ), QStringLiteral( "status" ), QStringLiteral( "고정상태" ) } );
    timeColumn = findHeader( headers, { QStringLiteral( "time" ), QStringLiteral( "datetime" ), QStringLiteral( "surveytime" ), QStringLiteral( "측량시간" ) } );
    accuracyColumn = findHeader( headers, {
                                             QStringLiteral( "hacc" ), QStringLiteral( "hacc(m)" ), QStringLiteral( "hacc(cm)" ), QStringLiteral( "hacc(mm)" ),
                                             QStringLiteral( "horizontalaccuracy" ), QStringLiteral( "horizontalaccuracy(m)" ), QStringLiteral( "horizontalaccuracy(cm)" ), QStringLiteral( "horizontalaccuracy(mm)" ),
                                             QStringLiteral( "haccuracy" ), QStringLiteral( "haccuracy(m)" ), QStringLiteral( "haccuracy(cm)" ), QStringLiteral( "haccuracy(mm)" ),
                                             QStringLiteral( "horizontalprecision" ), QStringLiteral( "horizontalprecision(m)" ), QStringLiteral( "horizontalprecision(cm)" ), QStringLiteral( "horizontalprecision(mm)" ),
                                             QStringLiteral( "hrms" ), QStringLiteral( "hrms(m)" ), QStringLiteral( "hrms(cm)" ), QStringLiteral( "hrms(mm)" ),
                                             QStringLiteral( "rms" ), QStringLiteral( "rms(m)" ), QStringLiteral( "rms(cm)" ), QStringLiteral( "rms(mm)" ),
                                             QStringLiteral( "accuracy" ), QStringLiteral( "accuracy(m)" ), QStringLiteral( "accuracy(cm)" ), QStringLiteral( "accuracy(mm)" ),
                                             QStringLiteral( "수평정확도" ), QStringLiteral( "수평정확도(m)" ), QStringLiteral( "수평정확도(cm)" ), QStringLiteral( "수평정확도(mm)" ),
                                             QStringLiteral( "정확도" ), QStringLiteral( "정확도(m)" ), QStringLiteral( "정확도(cm)" ), QStringLiteral( "정확도(mm)" ),
                                           } );
    if ( accuracyColumn >= 0 )
    {
      const QString accuracyHeader = normalized( headers.at( accuracyColumn ) );
      if ( accuracyHeader.endsWith( QStringLiteral( "mm" ) ) )
        accuracyScaleToMeters = 0.001;
      else if ( accuracyHeader.endsWith( QStringLiteral( "cm" ) ) )
        accuracyScaleToMeters = 0.01;
    }
  }
  if ( northColumn < 0 || eastColumn < 0 || elevationColumn < 0 )
    return errorResult( tr( "측점 파일에서 북ing(N)·동ing(E)·표고(Z) 열을 모두 찾지 못했습니다." ) );

  bool coordinatesAreWgs84 = false;
  if ( hasHeader )
  {
    const QString northHeader = normalized( headers.at( northColumn ) );
    const QString eastHeader = normalized( headers.at( eastColumn ) );
    const QSet<QString> latitudeHeaders = { QStringLiteral( "latitude" ), QStringLiteral( "lat" ), QStringLiteral( "위도" ) };
    const QSet<QString> longitudeHeaders = { QStringLiteral( "longitude" ), QStringLiteral( "lon" ), QStringLiteral( "경도" ) };
    coordinatesAreWgs84 = latitudeHeaders.contains( northHeader ) && longitudeHeaders.contains( eastHeader );
    if ( latitudeHeaders.contains( northHeader ) != longitudeHeaders.contains( eastHeader ) )
      return errorResult( tr( "경위도 머리글은 latitude/longitude(또는 lat/lon) 쌍으로 함께 있어야 합니다." ) );
  }

  QList<PointRecord> records;
  const int startRow = hasHeader ? 1 : 0;
  int skipped = 0;
  int fixedQuality = 0;
  int unverifiedQuality = 0;
  int rejectedQuality = 0;
  int verifiedAccuracy = 0;
  int unverifiedAccuracy = 0;
  for ( int row = startRow; row < lines.size(); ++row )
  {
    const QStringList values = splitLine( lines.at( row ), delimiter );
    if ( northColumn >= values.size() || eastColumn >= values.size() || elevationColumn >= values.size() )
    {
      ++skipped;
      continue;
    }
    bool northOk = false;
    bool eastOk = false;
    PointRecord record;
    record.northing = values.at( northColumn ).toDouble( &northOk );
    record.easting = values.at( eastColumn ).toDouble( &eastOk );
    if ( !northOk || !eastOk || !std::isfinite( record.northing ) || !std::isfinite( record.easting ) )
    {
      ++skipped;
      continue;
    }
    if ( coordinatesAreWgs84
         && ( record.easting < -180.0 || record.easting > 180.0
              || record.northing < -90.0 || record.northing > 90.0 ) )
      return errorResult( tr( "latitude/longitude 머리글의 좌표가 WGS84 유효 범위를 벗어났습니다." ) );
    if ( nameColumn >= 0 && nameColumn < values.size() )
      record.name = values.at( nameColumn ).trimmed();
    if ( record.name.isEmpty() )
    {
      ++skipped;
      continue;
    }
    record.elevation = values.at( elevationColumn ).toDouble( &record.elevationValid );
    if ( !record.elevationValid || !std::isfinite( record.elevation ) )
    {
      ++skipped;
      continue;
    }
    if ( codeColumn >= 0 && codeColumn < values.size() )
      record.code = values.at( codeColumn ).trimmed();
    if ( fixColumn >= 0 && fixColumn < values.size() )
      record.fixStatus = values.at( fixColumn ).trimmed();
    if ( timeColumn >= 0 && timeColumn < values.size() )
      record.surveyedAt = values.at( timeColumn ).trimmed();
    if ( accuracyColumn >= 0 && accuracyColumn < values.size() )
    {
      record.horizontalAccuracy = values.at( accuracyColumn ).toDouble( &record.horizontalAccuracyValid );
      record.horizontalAccuracyValid = record.horizontalAccuracyValid
                                       && std::isfinite( record.horizontalAccuracy )
                                       && record.horizontalAccuracy >= 0.0;
      if ( record.horizontalAccuracyValid )
        record.horizontalAccuracy *= accuracyScaleToMeters;
    }
    const FixQuality quality = classifyFixQuality( record.fixStatus );
    if ( quality == FixQuality::Rejected )
    {
      ++skipped;
      ++rejectedQuality;
      continue;
    }
    if ( quality == FixQuality::Fixed )
      ++fixedQuality;
    else
      ++unverifiedQuality;
    if ( record.horizontalAccuracyValid )
      ++verifiedAccuracy;
    else
      ++unverifiedAccuracy;
    records.append( record );
  }
  if ( records.isEmpty() )
    return errorResult( rejectedQuality > 0
                          ? tr( "FIX가 아닌 측점만 있어 가져오기를 중단했습니다. LandStar에서 RTK FIX 상태를 확인하세요." )
                          : tr( "유효한 LandStar 측점 좌표를 찾지 못했습니다." ) );

  QSet<QString> incomingNames;
  for ( const PointRecord &record : std::as_const( records ) )
  {
    const QString key = record.name.trimmed().toCaseFolded();
    if ( incomingNames.contains( key ) )
      return errorResult( tr( "측점 파일에 같은 측점명 '%1'이 두 번 이상 있습니다. 잘못된 점을 갱신하지 않도록 가져오기를 중단했습니다." ).arg( record.name ) );
    incomingNames.insert( key );
  }

  if ( !layer->crs().isValid() )
    return errorResult( tr( "성산 현장객체 레이어의 좌표계가 올바르지 않아 측점을 저장하지 않았습니다." ) );

  const QString targetCrsAuthId = layer->crs().authid();
  QString sourceCrsAuthId = QStringLiteral( "EPSG:4326" );
  if ( !coordinatesAreWgs84 )
  {
    bool confirmationRead = false;
    const bool crsConfirmed = project && project->readBoolEntry(
                                                QStringLiteral( "SungsanMobileGIS" ),
                                                QStringLiteral( "/landstarCrsConfirmed" ),
                                                false,
                                                &confirmationRead );
    const QString confirmedAuthId = project ? project->readEntry(
                                                QStringLiteral( "SungsanMobileGIS" ),
                                                QStringLiteral( "/landstarCrsAuthId" ),
                                                QString() )
                                            : QString();
    if ( layer->crs().isGeographic() || !confirmationRead || !crsConfirmed || confirmedAuthId.isEmpty() )
      return errorResult( tr( "투영 N/E 측점의 좌표계 확인 정보가 없습니다. QGIS 플러그인에서 LandStar 작업 좌표계와 프로젝트 좌표계가 같은지 확인한 뒤 현장 ZIP을 다시 만들어 주세요." ) );
    if ( confirmedAuthId.compare( targetCrsAuthId, Qt::CaseInsensitive ) != 0 )
      return errorResult( tr( "확인된 LandStar 좌표계(%1)와 현장객체 좌표계(%2)가 달라 측점을 저장하지 않았습니다." ).arg( confirmedAuthId, targetCrsAuthId ) );
    sourceCrsAuthId = confirmedAuthId;
  }

  const int idField = findField( layer, { QStringLiteral( "landstar_id" ) } );
  const int objectIdField = findField( layer, { QStringLiteral( "object_id" ) } );
  const int nameField = findField( layer, { QStringLiteral( "name" ) } );
  const int codeField = findField( layer, { QStringLiteral( "landstar_code" ), QStringLiteral( "code" ), QStringLiteral( "description" ), QStringLiteral( "desc" ), QStringLiteral( "category" ), QStringLiteral( "코드" ), QStringLiteral( "종류" ) } );
  const int northField = findField( layer, { QStringLiteral( "northing" ), QStringLiteral( "north" ), QStringLiteral( "n" ), QStringLiteral( "x_coord" ), QStringLiteral( "북ing" ) } );
  const int eastField = findField( layer, { QStringLiteral( "easting" ), QStringLiteral( "east" ), QStringLiteral( "e" ), QStringLiteral( "y_coord" ), QStringLiteral( "동ing" ) } );
  const int elevationField = findField( layer, { QStringLiteral( "elevation" ), QStringLiteral( "elev" ), QStringLiteral( "height" ), QStringLiteral( "z" ), QStringLiteral( "표고" ) } );
  const int fixField = findField( layer, { QStringLiteral( "fix_status" ), QStringLiteral( "fix" ), QStringLiteral( "solution" ), QStringLiteral( "quality" ), QStringLiteral( "고정상태" ) } );
  const int accuracyField = findField( layer, { QStringLiteral( "gps_accuracy_m" ), QStringLiteral( "hacc" ), QStringLiteral( "horizontal_accuracy" ), QStringLiteral( "accuracy" ), QStringLiteral( "수평정확도" ) } );
  const int timeField = findField( layer, { QStringLiteral( "surveyed_at" ), QStringLiteral( "survey_time" ), QStringLiteral( "measured_at" ), QStringLiteral( "측량시간" ) } );
  const int sourceField = findField( layer, { QStringLiteral( "source_device" ), QStringLiteral( "source" ), QStringLiteral( "자료출처" ) } );

  QHash<QString, QgsFeatureId> existingByName;
  QSet<QString> duplicateExistingNames;
  QSet<QgsFeatureId> existingMissingObjectId;
  QSet<QgsFeatureId> existingMissingName;
  if ( idField >= 0 )
  {
    QgsFeature existing;
    QgsFeatureRequest request;
    QgsAttributeList subsetOfAttributes { idField };
    if ( objectIdField >= 0 )
      subsetOfAttributes.append( objectIdField );
    if ( nameField >= 0 )
      subsetOfAttributes.append( nameField );
    request.setSubsetOfAttributes( subsetOfAttributes );
    request.setFlags( Qgis::FeatureRequestFlag::NoGeometry );
    QgsFeatureIterator iterator = layer->getFeatures( request );
    while ( iterator.nextFeature( existing ) )
    {
      const QString key = existing.attribute( idField ).toString().trimmed().toCaseFolded();
      if ( !key.isEmpty() )
      {
        if ( existingByName.contains( key ) )
          duplicateExistingNames.insert( key );
        else
          existingByName.insert( key, existing.id() );
      }
      if ( objectIdField >= 0 && existing.attribute( objectIdField ).toString().trimmed().isEmpty() )
        existingMissingObjectId.insert( existing.id() );
      if ( nameField >= 0 && existing.attribute( nameField ).toString().trimmed().isEmpty() )
        existingMissingName.insert( existing.id() );
    }
  }
  if ( !duplicateExistingNames.isEmpty() )
    return errorResult( tr( "기존 현장객체에 중복된 LandStar 측점명이 있어 안전하게 갱신할 수 없습니다. QGIS에서 중복 측점명을 먼저 정리하세요." ) );

  QString auditError;
  const QString auditPath = preserveLandStarSource( auditBytes, info, project, &auditError );
  if ( project && !project->homePath().trimmed().isEmpty() && auditPath.isEmpty() )
    return errorResult( auditError );

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
  const bool transformLonLat = coordinatesAreWgs84 && layer->crs() != wgs84;
  QgsCoordinateTransform coordinateTransform( wgs84, layer->crs(), project ? project->transformContext() : QgsCoordinateTransformContext() );

  for ( const PointRecord &record : std::as_const( records ) )
  {
    QgsPointXY mapPoint( record.easting, record.northing );
    if ( transformLonLat )
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
    if ( !std::isfinite( mapPoint.x() ) || !std::isfinite( mapPoint.y() ) )
    {
      ++skipped;
      continue;
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
    QSet<int> fieldsToClearOnUpdate;
    setValue( idField, record.name );
    if ( !record.code.isEmpty() )
      setValue( codeField, record.code );
    else if ( codeField >= 0 )
      fieldsToClearOnUpdate.insert( codeField );
    setValue( northField, record.northing );
    setValue( eastField, record.easting );
    if ( record.elevationValid )
      setValue( elevationField, record.elevation );
    if ( !record.fixStatus.isEmpty() )
      setValue( fixField, record.fixStatus );
    else
      setValue( fixField, QStringLiteral( "미제공" ) );
    if ( record.horizontalAccuracyValid )
      setValue( accuracyField, record.horizontalAccuracy );
    else if ( accuracyField >= 0 )
      fieldsToClearOnUpdate.insert( accuracyField );
    if ( !record.surveyedAt.isEmpty() )
      setValue( timeField, record.surveyedAt );
    else if ( timeField >= 0 )
      fieldsToClearOnUpdate.insert( timeField );
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
      for ( const int fieldIndex : std::as_const( fieldsToClearOnUpdate ) )
        layer->changeAttributeValue( featureId, fieldIndex, QVariant() );
      if ( existingMissingObjectId.remove( featureId ) )
        layer->changeAttributeValue( featureId, objectIdField, QUuid::createUuid().toString( QUuid::WithoutBraces ) );
      if ( existingMissingName.remove( featureId ) )
        layer->changeAttributeValue( featureId, nameField, record.name );
      ++updated;
    }
    else
    {
      setValue( objectIdField, QUuid::createUuid().toString( QUuid::WithoutBraces ) );
      setValue( nameField, record.name );
      if ( layer->addFeature( feature ) )
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
    { QStringLiteral( "fixedQuality" ), fixedQuality },
    { QStringLiteral( "unverifiedQuality" ), unverifiedQuality },
    { QStringLiteral( "rejectedQuality" ), rejectedQuality },
    { QStringLiteral( "verifiedAccuracy" ), verifiedAccuracy },
    { QStringLiteral( "unverifiedAccuracy" ), unverifiedAccuracy },
    { QStringLiteral( "coordinateMode" ), coordinatesAreWgs84 ? QStringLiteral( "WGS84" ) : QStringLiteral( "PROJECTED" ) },
    { QStringLiteral( "sourceCrs" ), sourceCrsAuthId },
    { QStringLiteral( "targetCrs" ), targetCrsAuthId },
    { QStringLiteral( "auditPath" ), auditPath },
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
