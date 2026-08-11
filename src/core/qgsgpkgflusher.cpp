/***************************************************************************
                          qgsgpkgflusher.cpp
                             -------------------
  begin                : Oct 2019
  copyright            : (C) 2019 by Matthias Kuhn
  email                : matthias@opengis.ch
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include "qgsgpkgflusher.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QTimer>
#include <qgsmessagelog.h>
#include <qgsproject.h>
#include <qgsprovidermetadata.h>
#include <qgsproviderregistry.h>
#include <qgsvectorlayer.h>

#include <sqlite3.h>

class Flusher : public QObject
{
    Q_OBJECT

  public slots:
    /**
     * Schedules a new flush for the given \a filename after 500ms.
     * If a new flush is scheduled for the same file before the actual flush is performed, the timer is reset to wait another 500ms.
     */
    void scheduleFlush( const QString &filename );

    /**
     * Flushes the contents of the given \a filename.
     */
    bool flush( const QString &filename, bool force = false );

    /**
     * Immediately performs a flush for a given \a fileName and returns. If the flusher is stopped, flush for that \a fileName would be ignored.
     */
    void stop( const QString &fileName );

    /**
     * Reenables scheduling flushes for a given \a fileName.
     */
    void start( const QString &fileName );

    /**
     * Returns whether the flusher is stopped for a given \a fileName.
     */
    bool isStopped( const QString &fileName ) const;

  private:
    QMutex mMutex;
    QMap<QString, QTimer *> mScheduledFlushes;
    QMap<QString, bool> mStoppedFlushes;
};

QgsGpkgFlusher::QgsGpkgFlusher( QgsProject *project )
  : QObject()
{
  connect( project, &QgsProject::layersAdded, this, &QgsGpkgFlusher::onLayersAdded );
  mFlusher = new Flusher();
  mFlusher->moveToThread( &mFlusherThread );
  connect( this, &QgsGpkgFlusher::requestFlush, mFlusher, &Flusher::scheduleFlush );
  mFlusherThread.start();
}

QgsGpkgFlusher::~QgsGpkgFlusher()
{
  mFlusherThread.quit();
  mFlusherThread.wait();
}

void QgsGpkgFlusher::onLayersAdded( const QList<QgsMapLayer *> &layers )
{
  for ( QgsMapLayer *layer : layers )
  {
    QgsVectorLayer *vl = dynamic_cast<QgsVectorLayer *>( layer );
    if ( vl && vl->dataProvider() )
    {
      QString dataSourceUri = vl->dataProvider()->dataSourceUri();
      QgsProviderMetadata *metadata = QgsProviderRegistry::instance()->providerMetadata( vl->dataProvider()->name() );
      const QString filePath = metadata->decodeUri( dataSourceUri ).value( QStringLiteral( "path" ) ).toString();
      if ( !filePath.endsWith( QStringLiteral( ".gpkg" ), Qt::CaseInsensitive ) && !filePath.endsWith( QStringLiteral( ".sqlite" ), Qt::CaseInsensitive ) )
      {
        continue;
      }

      const QFileInfo fi( filePath );
      if ( fi.isFile() )
      {
        connect( vl, &QgsVectorLayer::editingStopped, [this, filePath]() { emit requestFlush( filePath ); } );
      }
    }
  }
}

void QgsGpkgFlusher::stop( const QString &fileName )
{
  if ( QThread::currentThread() == mFlusher->thread() )
  {
    mFlusher->stop( fileName );
  }
  else
  {
    QMetaObject::invokeMethod( mFlusher, [this, fileName]() { mFlusher->stop( fileName ); }, Qt::BlockingQueuedConnection );
  }
}

void QgsGpkgFlusher::start( const QString &fileName )
{
  if ( QThread::currentThread() == mFlusher->thread() )
  {
    mFlusher->start( fileName );
  }
  else
  {
    QMetaObject::invokeMethod( mFlusher, [this, fileName]() { mFlusher->start( fileName ); }, Qt::BlockingQueuedConnection );
  }
}

bool QgsGpkgFlusher::isStopped( const QString &fileName ) const
{
  if ( QThread::currentThread() == mFlusher->thread() )
  {
    return mFlusher->isStopped( fileName );
  }

  bool stopped = false;
  const bool invoked = QMetaObject::invokeMethod( mFlusher, [this, fileName, &stopped]() { stopped = mFlusher->isStopped( fileName ); }, Qt::BlockingQueuedConnection );
  return invoked && stopped;
}

bool QgsGpkgFlusher::flush( const QString &fileName )
{
  if ( QThread::currentThread() == mFlusher->thread() )
  {
    return mFlusher->flush( fileName, true );
  }

  bool succeeded = false;
  const bool invoked = QMetaObject::invokeMethod( mFlusher, [this, fileName, &succeeded]() { succeeded = mFlusher->flush( fileName, true ); }, Qt::BlockingQueuedConnection );
  return invoked && succeeded;
}

bool QgsGpkgFlusher::flushDirectory( const QString &directoryPath )
{
  const QFileInfo directoryInfo( directoryPath );
  if ( !directoryInfo.isDir() )
  {
    return false;
  }

  bool succeeded = true;
  QDirIterator it( directoryInfo.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories );
  while ( it.hasNext() )
  {
    const QFileInfo fileInfo = it.nextFileInfo();
    const QString suffix = fileInfo.suffix().toLower();
    if ( suffix != QStringLiteral( "gpkg" ) && suffix != QStringLiteral( "sqlite" ) )
    {
      continue;
    }

    const QString fileName = fileInfo.absoluteFilePath();
    if ( QFileInfo::exists( fileName + QStringLiteral( "-wal" ) ) )
    {
      succeeded = flush( fileName ) && succeeded;
    }
  }
  return succeeded;
}

void Flusher::scheduleFlush( const QString &filename )
{
  if ( mStoppedFlushes.value( filename, false ) )
    return;

  if ( mScheduledFlushes.contains( filename ) )
  {
    mScheduledFlushes.value( filename )->start( 500 );
  }
  else
  {
    QTimer *timer = new QTimer( this );
    connect( timer, &QTimer::timeout, this, [this, filename]() { flush( filename ); } );
    timer->setSingleShot( true );
    mScheduledFlushes.insert( filename, timer );
    timer->start( 500 );
  }
}

bool Flusher::flush( const QString &filename, bool force )
{
  if ( !force && mStoppedFlushes.value( filename, false ) )
    return true;

  QMutexLocker locker( &mMutex );

  sqlite3_database_unique_ptr db;
  int status = db.open_v2( filename, SQLITE_OPEN_READWRITE, nullptr );
  if ( status != SQLITE_OK )
  {
    QgsMessageLog::logMessage( QObject::tr( "There was an error opening the database <b>%1</b>: %2" ).arg( filename, db.errorMessage() ) );
    return false;
  }

  int logFrames = -1;
  int checkpointedFrames = -1;
  status = sqlite3_wal_checkpoint_v2( db.get(), nullptr, SQLITE_CHECKPOINT_FULL, &logFrames, &checkpointedFrames );

  if ( status == SQLITE_OK && ( logFrames < 0 || checkpointedFrames >= logFrames ) )
  {
    if ( QTimer *timer = mScheduledFlushes.take( filename ) )
    {
      timer->deleteLater();
    }
    return true;
  }

  QgsMessageLog::logMessage(
    QObject::tr( "Could not fully checkpoint database %1 (%2; %3 of %4 WAL frames)" )
      .arg( filename, db.errorMessage() )
      .arg( checkpointedFrames )
      .arg( logFrames )
  );
  if ( QTimer *timer = mScheduledFlushes.value( filename, nullptr ) )
  {
    timer->start( 500 );
  }
  return false;
}

void Flusher::stop( const QString &fileName )
{
  if ( !mScheduledFlushes.contains( fileName ) )
    return;

  if ( QTimer *timer = mScheduledFlushes.take( fileName ) )
  {
    timer->stop();
    timer->deleteLater();
  }

  flush( fileName );

  mStoppedFlushes.insert( fileName, true );
}

void Flusher::start( const QString &fileName )
{
  mStoppedFlushes.remove( fileName );
}

bool Flusher::isStopped( const QString &fileName ) const
{
  return mStoppedFlushes.value( fileName, false );
}

#include "qgsgpkgflusher.moc"
