/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "utils/landstarpointfile.h"

#include <cassert>

int main()
{
  LandStarPointFile::Parsed result;
  const QString csv = QStringLiteral( "no,x,y,z,code\r\nP-01,200,100,9.4,MH\r\n" );
  assert( LandStarPointFile::parse( csv, result ).isEmpty() );
  assert( result.records.size() == 1 && !result.coordinatesAreWgs84 );
  const auto first = result.records.first();
  assert( first.name == QStringLiteral( "P-01" ) && first.code == QStringLiteral( "MH" ) );
  assert( first.northing == 200 && first.easting == 100 && first.elevation == 9.4 );
  const QString tabs = QStringLiteral( "no\tcode\tx\ty\tz\tfix\taccuracy(cm)\nP-02\t\t222\t111\t10\tFIX\t2.5\nP-03\tX\t223\t112\t10\tFLOAT\t3" );
  assert( LandStarPointFile::parse( tabs, result ).isEmpty() );
  assert( result.records.size() == 1 && result.records.first().code.isEmpty() );
  assert( result.records.first().northing == 222 && result.records.first().easting == 111 );
  assert( result.records.first().horizontalAccuracy == 0.025 && result.fixedQuality == 1 && result.rejectedQuality == 1 );
  assert( LandStarPointFile::parse( QStringLiteral( "P-04 203 101 10.2 K" ), result ).isEmpty() );
  assert( result.records.first().name == QStringLiteral( "P-04" ) );
  assert( !LandStarPointFile::parse( csv + QStringLiteral( "p-01,201,101,10,N" ), result ).isEmpty() );
  assert( !LandStarPointFile::parse( QStringLiteral( "no,lat,lon,z,code\nP4,1000,2000,10,K" ), result ).isEmpty() );
  assert( LandStarPointFile::parse( QStringLiteral( "no,lat,lon,z,code\nP4,35,129,10,K" ), result ).isEmpty() && result.coordinatesAreWgs84 );
}
