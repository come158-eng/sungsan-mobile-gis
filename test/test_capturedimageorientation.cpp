/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "capturedimageorientation.h"
#include "utils/managedphotostore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

static void expect( bool ok, const char *message )
{
  if ( !ok )
  {
    std::cerr << message << '\n';
    std::exit( 1 );
  }
}

// Asymmetric colour chart: a red square starts at the sensor's top left.
static QByteArray jpeg( QSize size, int exif )
{
  QImage image( size, QImage::Format_RGB32 );
  image.fill( Qt::white );
  for ( int y = 0; y < 16; ++y )
    for ( int x = 0; x < 16; ++x )
      image.setPixelColor( x, y, Qt::red );
  QByteArray bytes;
  QBuffer buffer( &bytes );
  expect( buffer.open( QIODevice::WriteOnly ) && image.save( &buffer, "JPEG", 98 ), "JPEG encoder unavailable" );
  if ( exif )
  {
    // APP1 EXIF, big-endian TIFF with one SHORT (0x0112, orientation).
    QByteArray header = QByteArray::fromHex( "ffe100224578696600004d4d002a00000008000101120003000000010000000000000000" );
    header[29] = static_cast<char>( exif );
    bytes.insert( 2, header );
  }
  return bytes;
}

static void checkImage( const QString &root, int slot, QSize raw, int exif, Qt::ScreenOrientation orientation,
                        QSize expected, QPoint redCorner )
{
  const QString capture = root + "/capture.jpg";
  const QString target = root + QStringLiteral( "/images/P01 (%1).jpg" ).arg( slot );
  const QByteArray bytes = jpeg( raw, exif );
  QFile file( capture );
  expect( file.open( QIODevice::WriteOnly ) && file.write( bytes ) == bytes.size(), "Capture fixture failed" );
  file.close();
  expect( prepareCapturedImage( capture, orientation, true ), "Failed to prepare JPEG" );
  expect( ManagedPhotoStore::save( capture, target, root ).value( "ok" ).toBool(), "Failed to store oriented image" );
  QImageReader reader( target );
  reader.setAutoTransform( false );
  expect( reader.transformation() == QImageIOHandler::TransformationNone, "Export depends on EXIF rotation" );
  const QImage image = reader.read();
  expect( image.size() == expected, "Unexpected stored dimensions" );
  const QColor color = image.pixelColor( redCorner );
  expect( color.red() > 210 && color.green() < 40 && color.blue() < 40, "JPEG rotated in the wrong direction" );
}

int main( int argc, char **argv )
{
  QCoreApplication app( argc, argv );
  QTemporaryDir project;
  expect( project.isValid(), "Test folder unavailable" );
  const QSize landscape( 96, 64 );
  const QSize portrait( 64, 96 );
  checkImage( project.path(), 1, landscape, 0, Qt::LandscapeOrientation, landscape, { 7, 7 } );
  checkImage( project.path(), 2, portrait, 0, Qt::PortraitOrientation, portrait, { 7, 7 } );
  checkImage( project.path(), 3, landscape, 6, Qt::LandscapeOrientation, portrait, { 56, 7 } );
  checkImage( project.path(), 4, portrait, 8, Qt::PortraitOrientation, landscape, { 7, 56 } );
  checkImage( project.path(), 1, landscape, 0, Qt::PortraitOrientation, portrait, { 56, 7 } );
  checkImage( project.path(), 2, portrait, 0, Qt::LandscapeOrientation, landscape, { 7, 56 } );
  checkImage( project.path(), 3, landscape, 0, Qt::InvertedPortraitOrientation, portrait, { 7, 88 } );
  checkImage( project.path(), 4, portrait, 0, Qt::InvertedLandscapeOrientation, landscape, { 88, 7 } );
  expect( !prepareCapturedImage( project.filePath( "missing.jpg" ), Qt::PortraitOrientation, true ), "Accepted unreadable image" );
  std::cout << "PASS: eight stored JPEGs checked for decoded dimensions, red marker direction and neutral EXIF\n";
}
