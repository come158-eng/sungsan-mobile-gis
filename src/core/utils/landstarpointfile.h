/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <utility>

namespace LandStarPointFile
{
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

  enum class FixQuality
  {
    Fixed,
    Rejected,
    Unverified,
  };

  inline QString normalized( const QString &value )
  {
    QString result = value.trimmed().toLower();
    result.remove( QRegularExpression( QStringLiteral( "[\\s_\\-()\\[\\].]" ) ) );
    return result;
  }

  inline QStringList parseCsvLine( const QString &line, const QChar delimiter )
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

  inline QStringList splitLine( const QString &line, const QChar delimiter )
  {
    if ( delimiter == QLatin1Char( ' ' ) )
      return line.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
    return parseCsvLine( line, delimiter );
  }

  inline int findHeader( const QStringList &headers, const QStringList &aliases )
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

  inline FixQuality classifyFixQuality( const QString &value )
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

  struct Parsed
  {
    QList<PointRecord> records;
    bool coordinatesAreWgs84 = false;
    int skipped = 0;
    int fixedQuality = 0;
    int unverifiedQuality = 0;
    int rejectedQuality = 0;
    int verifiedAccuracy = 0;
    int unverifiedAccuracy = 0;
  };

  inline QString parse( const QString &text, Parsed &out )
  {
    out = Parsed();
    QStringList lines = text.split( QRegularExpression( QStringLiteral( "\\r?\\n" ) ), Qt::SkipEmptyParts );
    lines.erase( std::remove_if( lines.begin(), lines.end(), []( const QString &line ) {
                   const QString trimmed = line.trimmed();
                   return trimmed.isEmpty() || trimmed.startsWith( QLatin1Char( '#' ) ) || trimmed.startsWith( QStringLiteral( "//" ) );
                 } ),
                 lines.end() );
    if ( lines.isEmpty() )
      return QObject::tr( "LandStar 측점 파일에 읽을 행이 없습니다." );
    if ( lines.size() > MAX_POINT_ROWS + 1 )
      return QObject::tr( "LandStar 측점 수가 안전 제한(200,000개)을 초과했습니다." );

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
      nameColumn = findHeader( headers, { QStringLiteral( "no" ), QStringLiteral( "name" ), QStringLiteral( "point" ), QStringLiteral( "pointname" ), QStringLiteral( "pointid" ), QStringLiteral( "id" ), QStringLiteral( "pt" ), QStringLiteral( "측점" ), QStringLiteral( "점번호" ), QStringLiteral( "측점명" ) } );
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
    if ( nameColumn < 0 )
      return QObject::tr( "측점 파일에서 측점명(no) 열을 찾지 못했습니다." );
    if ( northColumn < 0 || eastColumn < 0 || elevationColumn < 0 )
      return QObject::tr( "측점 파일에서 북ing(N)·동ing(E)·표고(Z) 열을 모두 찾지 못했습니다." );

    if ( hasHeader )
    {
      const QString northHeader = normalized( headers.at( northColumn ) );
      const QString eastHeader = normalized( headers.at( eastColumn ) );
      const QSet<QString> latitudeHeaders = { QStringLiteral( "latitude" ), QStringLiteral( "lat" ), QStringLiteral( "위도" ) };
      const QSet<QString> longitudeHeaders = { QStringLiteral( "longitude" ), QStringLiteral( "lon" ), QStringLiteral( "경도" ) };
      out.coordinatesAreWgs84 = latitudeHeaders.contains( northHeader ) && longitudeHeaders.contains( eastHeader );
      if ( latitudeHeaders.contains( northHeader ) != longitudeHeaders.contains( eastHeader ) )
        return QObject::tr( "경위도 머리글은 latitude/longitude(또는 lat/lon) 쌍으로 함께 있어야 합니다." );
    }

    const int startRow = hasHeader ? 1 : 0;
    for ( int row = startRow; row < lines.size(); ++row )
    {
      const QStringList values = splitLine( lines.at( row ), delimiter );
      if ( northColumn >= values.size() || eastColumn >= values.size() || elevationColumn >= values.size() )
      {
        ++out.skipped;
        continue;
      }
      bool northOk = false;
      bool eastOk = false;
      PointRecord record;
      record.northing = values.at( northColumn ).toDouble( &northOk );
      record.easting = values.at( eastColumn ).toDouble( &eastOk );
      if ( !northOk || !eastOk || !std::isfinite( record.northing ) || !std::isfinite( record.easting ) )
      {
        ++out.skipped;
        continue;
      }
      if ( out.coordinatesAreWgs84
           && ( record.easting < -180.0 || record.easting > 180.0
                || record.northing < -90.0 || record.northing > 90.0 ) )
        return QObject::tr( "latitude/longitude 머리글의 좌표가 WGS84 유효 범위를 벗어났습니다." );
      if ( nameColumn >= 0 && nameColumn < values.size() )
        record.name = values.at( nameColumn ).trimmed();
      if ( record.name.isEmpty() )
      {
        ++out.skipped;
        continue;
      }
      record.elevation = values.at( elevationColumn ).toDouble( &record.elevationValid );
      if ( !record.elevationValid || !std::isfinite( record.elevation ) )
      {
        ++out.skipped;
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
        ++out.skipped;
        ++out.rejectedQuality;
        continue;
      }
      if ( quality == FixQuality::Fixed )
        ++out.fixedQuality;
      else
        ++out.unverifiedQuality;
      if ( record.horizontalAccuracyValid )
        ++out.verifiedAccuracy;
      else
        ++out.unverifiedAccuracy;
      out.records.append( record );
    }
    if ( out.records.isEmpty() )
      return out.rejectedQuality > 0
                            ? QObject::tr( "FIX가 아닌 측점만 있어 가져오기를 중단했습니다. LandStar에서 RTK FIX 상태를 확인하세요." )
                            : QObject::tr( "유효한 LandStar 측점 좌표를 찾지 못했습니다." );

    QSet<QString> incomingNames;
    for ( const PointRecord &record : std::as_const( out.records ) )
    {
      const QString key = record.name.trimmed().toCaseFolded();
      if ( incomingNames.contains( key ) )
        return QObject::tr( "측점 파일에 같은 측점명 '%1'이 두 번 이상 있습니다. 잘못된 점을 갱신하지 않도록 가져오기를 중단했습니다." ).arg( record.name );
      incomingNames.insert( key );
    }
    return QString();
  }
}
