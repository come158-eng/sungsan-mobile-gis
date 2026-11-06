/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "utils/managedphotostore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

static void require( bool success, const char *message )
{
  if ( !success ) { std::cerr << message << '\n'; std::exit( 1 ); }
}
static void write( const QString &path, const QByteArray &bytes )
{
  QFile file( path );
  require( file.open( QIODevice::WriteOnly ), "Fixture open failed" );
  require( file.write( bytes ) == bytes.size(), "Fixture write failed" );
}
static QByteArray read( const QString &path )
{
  QFile file( path );
  require( file.open( QIODevice::ReadOnly ), "Read failed" );
  return file.readAll();
}
int main( int argc, char **argv )
{
  QCoreApplication app( argc, argv );
  QTemporaryDir project;
  QTemporaryDir outside;
  require( project.isValid() && outside.isValid(), "Temporary directories unavailable" );
  const QString root = project.path();
  const QString source = root + "/capture.jpg";
  const QString photo = root + QString::fromUtf8( "/images/현장객체/맨홀 01 (1).jpg" );
  const QByteArray first( 150000, 'A' );
  const QByteArray second( 175000, 'B' );
  write( source, first );
  require( ManagedPhotoStore::save( QUrl::fromLocalFile( source ).toString(), QUrl::fromLocalFile( photo ).toString(), root )["ok"].toBool(), "First save with file URLs failed" );
  require( read( photo ) == first && !QFileInfo::exists( source ), "First save lost data" );
  write( source, second );
  require( ManagedPhotoStore::save( source, photo, root )["ok"].toBool(), "Retake failed" );
  require( read( photo ) == second, "Retake lost replacement" );
  require( !ManagedPhotoStore::save( source, photo, root )["ok"].toBool() && read( photo ) == second, "Missing source destroyed old photo" );
  write( source, QByteArray() );
  require( !ManagedPhotoStore::save( source, photo, root )["ok"].toBool() && read( photo ) == second, "Empty source destroyed old photo" );
  write( source, first );
  require( !ManagedPhotoStore::save( source, outside.path() + "/escape.jpg", root )["ok"].toBool(), "Outside project accepted" );
  require( read( source ) == first && read( photo ) == second, "Failed save removed data" );
  require( !ManagedPhotoStore::save( source, root + "/../escape.jpg", root )["ok"].toBool(), "Traversal accepted" );
  require( !ManagedPhotoStore::save( source, QFileInfo( photo ).absolutePath(), root )["ok"].toBool(), "Directory replaced" );
  require( ManagedPhotoStore::save( photo, photo, root )["ok"].toBool() && read( photo ) == second, "Same-path retake removed photo" );
#ifndef Q_OS_WIN
  require( QFile::link( outside.path(), root + "/linked" ), "Symlink fixture failed" );
  require( !ManagedPhotoStore::save( source, root + "/linked/new/nested/escape.jpg", root )["ok"].toBool(), "Symlink escape accepted" );
#endif
  write( root + "/blocked", "not a directory" );
  require( !ManagedPhotoStore::save( source, root + "/blocked/sub/photo.jpg", root )["ok"].toBool(), "Invalid parent accepted" );
  require( read( source ) == first && read( photo ) == second, "Failure deleted source or old photo" );
  std::cout << "PASS: production Qt photo transaction; first capture, retake, URL/Unicode, failure preservation, traversal/symlinks\n";
}
