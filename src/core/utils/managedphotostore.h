/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUuid>
#include <QUrl>
#include <QVariantMap>

// Pure Qt file transaction, also exercised by the host-side regression test.
namespace ManagedPhotoStore
{
  inline QString localPath( const QString &path )
  {
    const QUrl url( path );
    return url.isLocalFile() ? url.toLocalFile() : path;
  }

  inline bool inside( const QString &root, const QString &path )
  {
    const QString canonicalRoot = QFileInfo( root ).canonicalFilePath();
    if ( canonicalRoot.isEmpty() )
      return false;
    QFileInfo entry( path );
    QStringList missing;
    while ( !entry.exists() && !entry.isSymLink() )
    {
      missing.prepend( entry.fileName() );
      const QFileInfo parent( entry.absolutePath() );
      if ( parent.absoluteFilePath() == entry.absoluteFilePath() )
        return false;
      entry = parent;
    }
    QString resolved = entry.canonicalFilePath();
    if ( resolved.isEmpty() )
      return false; // Includes dangling symlinks.
    for ( const QString &segment : missing )
      resolved = QDir( resolved ).absoluteFilePath( segment );
    resolved = QDir::cleanPath( resolved );
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return resolved.compare( canonicalRoot, sensitivity ) == 0
           || resolved.startsWith( canonicalRoot + '/', sensitivity );
  }

  inline QVariantMap save( const QString &sourceName, const QString &destinationName, const QString &projectDirectory )
  {
    auto failure = []( const QString &code, const QString &error ) {
      return QVariantMap{ { QStringLiteral( "ok" ), false }, { QStringLiteral( "code" ), code }, { QStringLiteral( "error" ), error } };
    };
    const QString sourcePath = localPath( sourceName );
    const QString destinationPath = localPath( destinationName );
    if ( sourcePath.isEmpty() || destinationPath.isEmpty() || !QDir::isAbsolutePath( destinationPath )
         || !inside( projectDirectory, destinationPath ) )
      return failure( QStringLiteral( "destination" ), QStringLiteral( "사진 저장 경로가 프로젝트 폴더 안에 있지 않습니다." ) );
    QFile source( sourcePath );
    if ( !QFileInfo( sourcePath ).isFile() || !source.open( QIODevice::ReadOnly ) || source.size() <= 0 )
      return failure( QStringLiteral( "source" ), QStringLiteral( "촬영 파일이 아직 준비되지 않았거나 읽을 수 없습니다." ) );
    const qint64 expected = source.size();
    if ( QFileInfo( sourcePath ).canonicalFilePath() == QFileInfo( destinationPath ).canonicalFilePath()
         && QFileInfo( destinationPath ).exists() )
      return { { QStringLiteral( "ok" ), true } };
    const QFileInfo destinationInfo( destinationPath );
    if ( destinationInfo.exists() && !destinationInfo.isFile() )
      return failure( QStringLiteral( "destination" ), QStringLiteral( "사진 이름과 같은 폴더가 있어 저장할 수 없습니다." ) );
    if ( !QDir().mkpath( destinationInfo.absolutePath() ) || !inside( projectDirectory, destinationPath ) )
      return failure( QStringLiteral( "mkdir" ), QStringLiteral( "사진 폴더를 만들 수 없습니다. 저장 공간과 권한을 확인해 주세요." ) );

    QTemporaryFile staged( destinationInfo.absolutePath() + QStringLiteral( "/.capture-XXXXXX" ) );
    if ( !staged.open() )
      return failure( QStringLiteral( "stage" ), staged.errorString() );
    qint64 written = 0;
    while ( !source.atEnd() )
    {
      const QByteArray bytes = source.read( 64 * 1024 );
      if ( bytes.isEmpty() || staged.write( bytes ) != bytes.size() )
        return failure( QStringLiteral( "copy" ), QStringLiteral( "사진 파일을 끝까지 복사하지 못했습니다. 저장 공간을 확인해 주세요." ) );
      written += bytes.size();
    }
    if ( written != expected || !staged.flush() )
      return failure( QStringLiteral( "copy" ), QStringLiteral( "촬영 사진의 크기 검증에 실패했습니다." ) );
    source.close();
    staged.close();

    // A new photo needs only a same-directory rename. A retake keeps the old
    // attachment under a unique recovery name until the new file is verified.
    const QString backup = destinationPath + QStringLiteral( ".previous-" ) + QUuid::createUuid().toString( QUuid::WithoutBraces );
    const bool hadPrevious = QFileInfo::exists( destinationPath );
    if ( hadPrevious && !QFile::rename( destinationPath, backup ) )
      return failure( QStringLiteral( "backup" ), QStringLiteral( "기존 사진을 보호할 수 없어 교체를 중단했습니다." ) );
    if ( !QFile::rename( staged.fileName(), destinationPath ) )
    {
      if ( hadPrevious && !QFile::rename( backup, destinationPath ) )
        return failure( QStringLiteral( "restore" ), QStringLiteral( "기존 사진은 .previous 보관 파일에 남아 있습니다. 이 프로젝트를 보존해 주세요." ) );
      return failure( QStringLiteral( "commit" ), QStringLiteral( "사진을 최종 폴더로 옮기지 못했습니다. 다시 저장해 주세요." ) );
    }
    staged.setAutoRemove( false );
    if ( QFileInfo( destinationPath ).size() != expected )
    {
      QFile::remove( destinationPath );
      if ( hadPrevious )
        QFile::rename( backup, destinationPath );
      return failure( QStringLiteral( "verify" ), QStringLiteral( "저장된 사진을 검증하지 못했습니다." ) );
    }
    if ( hadPrevious )
      QFile::remove( backup );
    QFile::remove( sourcePath );
    return { { QStringLiteral( "ok" ), true }, { QStringLiteral( "path" ), destinationPath } };
  }
}
