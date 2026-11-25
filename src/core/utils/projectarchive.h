/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "managedphotostore.h"

#include <QSaveFile>
#include <zip.h>

namespace ProjectArchive
{
  inline QString extract( const QString &archivePath, const QString &folder, QStringList &files, bool checkConsistency )
  {
    files.clear();
    if ( !QFileInfo( folder ).isDir() )
      return QObject::tr( "ZIP destination directory does not exist: %1" ).arg( folder );
    int status = 0;
    std::unique_ptr<zip_t, decltype( &zip_discard )> archive( zip_open( archivePath.toUtf8().constData(), ZIP_RDONLY | ( checkConsistency ? ZIP_CHECKCONS : 0 ), &status ), &zip_discard );
    if ( !archive )
      return QObject::tr( "Could not open ZIP %1 (error %2)" ).arg( archivePath ).arg( status );
    const zip_int64_t count = zip_get_num_entries( archive.get(), 0 );
    if ( count < 0 )
      return QString::fromUtf8( zip_strerror( archive.get() ) );

    const QDir root( folder );
    QStringList completed;
    char buffer[64 * 1024];
    for ( zip_int64_t i = 0; i < count; ++i )
    {
      zip_stat_t entry;
      if ( zip_stat_index( archive.get(), i, 0, &entry ) < 0 || !( entry.valid & ZIP_STAT_NAME ) || !( entry.valid & ZIP_STAT_SIZE ) )
        return QObject::tr( "Could not read ZIP entry %1." ).arg( i );
      const QString name = QString::fromUtf8( entry.name );
      const QString clean = QDir::cleanPath( name );
      if ( QDir::isAbsolutePath( name ) || clean == QStringLiteral( ".." ) || clean.startsWith( QStringLiteral( "../" ) ) || name.contains( QLatin1Char( '\\' ) ) || name.contains( QLatin1Char( ':' ) ) )
        return QObject::tr( "Invalid path in ZIP: %1" ).arg( name );
      const QString path = root.absoluteFilePath( clean );
      if ( !ManagedPhotoStore::inside( folder, path ) )
        return QObject::tr( "ZIP path is outside the destination: %1" ).arg( name );
      const bool directory = name.endsWith( QLatin1Char( '/' ) );
      if ( !root.mkpath( directory ? path : QFileInfo( path ).absolutePath() ) )
        return QObject::tr( "Could not create the ZIP destination for %1." ).arg( name );
      if ( directory )
        continue;

      std::unique_ptr<zip_file_t, decltype( &zip_fclose )> input( zip_fopen_index( archive.get(), i, 0 ), &zip_fclose );
      if ( !input )
        return QString::fromUtf8( zip_strerror( archive.get() ) );
      QSaveFile output( path );
      if ( !output.open( QIODevice::WriteOnly ) )
        return output.errorString();
      zip_uint64_t written = 0;
      zip_int64_t bytes = 0;
      while ( ( bytes = zip_fread( input.get(), buffer, sizeof buffer ) ) > 0 )
      {
        if ( output.write( buffer, bytes ) != bytes )
          return output.errorString();
        written += static_cast<zip_uint64_t>( bytes );
      }
      if ( bytes < 0 || written != entry.size || zip_fclose( input.release() ) != 0 )
        return QObject::tr( "Could not fully read %1 from the ZIP." ).arg( name );
      if ( !output.commit() )
        return output.errorString();
      completed.append( path );
    }
    files = completed;
    return QString();
  }
}
