// SPDX-License-Identifier: GPL-2.0-or-later
#include "platforms/android/androidprojectopenrequests.h"

#include <QCoreApplication>
#include <QThread>
#include <cassert>
#include <thread>

int main( int argc, char **argv )
{
  QCoreApplication app( argc, argv );
  AndroidProjectOpenRequests requests;
  const QString early = QString::fromUtf8( u8"현장📍/1111.qgz" );
  const QString missing = QStringLiteral( "missing.qgz" );
  const QString next = QStringLiteral( "another project.qgs" );
  QStringList opened;

  // Hold the receiver event loop until the Activity callback returns. A
  // blocking connection here makes this executable hit its external timeout.
  std::thread activity( [&]() {
    requests.enqueue( early );
    requests.enqueue( early );
  } );
  activity.join();
  requests.start( &app, [&]( const QString &path ) {
    assert( QThread::currentThread() == app.thread() );
    opened.append( path );
    return path != missing;
  } );
  std::thread returningActivity( [&]() {
    requests.enqueue( early );
    requests.enqueue( missing );
    requests.enqueue( next );
  } );
  returningActivity.join();
  assert( opened.isEmpty() );
  QCoreApplication::sendPostedEvents();
  assert( opened == QStringList{early} );
  requests.enqueue( early );
  requests.finished(); // delayed QML load completion
  QCoreApplication::sendPostedEvents();
  QCoreApplication::sendPostedEvents();
  assert( opened == ( QStringList{early, missing, next} ) );
  requests.finished();
  requests.enqueue( missing ); // failed path is retryable
  QCoreApplication::sendPostedEvents();
  assert( opened == ( QStringList{early, missing, next, missing} ) );
  requests.enqueue( QString() );

  AndroidProjectOpenRequests earlyAgain;
  {
    QObject temporaryReceiver;
    earlyAgain.start( &temporaryReceiver, []( const QString & ) { assert( false ); return false; } );
    earlyAgain.enqueue( early );
  }
  QCoreApplication::sendPostedEvents();
  earlyAgain.start( &app, [&]( const QString &path ) { assert( path == early ); opened.append( path ); return false; } );
  QCoreApplication::sendPostedEvents();
  assert( opened.size() == 5 );
}
