// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ANDROIDPROJECTOPENREQUESTS_H
#define ANDROIDPROJECTOPENREQUESTS_H

#include <QMutex>
#include <QObject>
#include <QStringList>

// Android may send a path before the Qt scene exists, or while it is reading another project.
class AndroidProjectOpenRequests : public QObject
{
    Q_OBJECT

  public:
    void enqueue( const QString &path )
    {
      {
        QMutexLocker lock( &mMutex );
        if ( path.isEmpty() || mPaths.contains( path ) )
          return;
        mPaths.append( path );
      }
      emit available();
    }

    template <typename Open>
    void start( QObject *receiver, Open open )
    {
      connect( this, &AndroidProjectOpenRequests::available, receiver, [this, open]() {
        QString path;
        {
          QMutexLocker lock( &mMutex );
          if ( mOpening || mPaths.isEmpty() )
            return;
          mOpening = true;
          path = mPaths.constFirst();
        }
        // A successful loadFile starts the QML loader. Keep this request active
        // until loadProjectEnded, including when QGIS reports a read failure.
        if ( !open( path ) )
          finished();
      }, Qt::QueuedConnection );
      emit available();
    }

    void finished()
    {
      {
        QMutexLocker lock( &mMutex );
        if ( !mOpening )
          return;
        mPaths.removeFirst();
        mOpening = false;
        if ( mPaths.isEmpty() )
          return;
      }
      emit available();
    }

  signals:
    void available();

  private:
    QMutex mMutex;
    QStringList mPaths;
    bool mOpening = false;
};

#endif
