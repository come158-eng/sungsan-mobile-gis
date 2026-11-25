/***************************************************************************
  cameraorientationnormalizer.cpp - CameraOrientationNormalizer

 ---------------------
 begin                : 16.4.2026
 copyright            : (C) 2026 by Kaustuv Pokharel
 email                : kaustuv@opengis.ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "cameraorientationnormalizer.h"

#include <QGuiApplication>
#include "capturedimageorientation.h"
#include <QScreen>

#include <QOrientationReading>

CameraOrientationNormalizer::CameraOrientationNormalizer( QObject *parent )
  : QObject( parent )
{
  QScreen *screen = QGuiApplication::primaryScreen();
  if ( screen )
  {
    mCurrentOrientation = screen->orientation();
    connect( screen, &QScreen::orientationChanged, this, &CameraOrientationNormalizer::handleScreenOrientationChanged );
  }
}

int CameraOrientationNormalizer::previewRotation() const
{
  return mPreviewRotation;
}

void CameraOrientationNormalizer::recordCaptureOrientation()
{
  QScreen *screen = QGuiApplication::primaryScreen();
  mCaptureOrientation = screen ? screen->orientation() : Qt::PortraitOrientation;
  mCaptureOrientationRecorded = true;
}

void CameraOrientationNormalizer::recordCaptureViewportOrientation( bool landscape )
{
  QScreen *screen = QGuiApplication::primaryScreen();
  const Qt::ScreenOrientation screenOrientation = screen ? screen->orientation() : Qt::PrimaryOrientation;
  const bool screenIsLandscape = screenOrientation == Qt::LandscapeOrientation
                                 || screenOrientation == Qt::InvertedLandscapeOrientation;
  const bool screenIsPortrait = screenOrientation == Qt::PortraitOrientation
                                || screenOrientation == Qt::InvertedPortraitOrientation;

  // Preserve the clockwise/counter-clockwise distinction whenever Qt reports
  // an orientation consistent with the visible viewport. If Android keeps the
  // screen enum locked, the viewport still gives us the correct aspect.
  if ( ( landscape && screenIsLandscape ) || ( !landscape && screenIsPortrait ) )
    mCaptureOrientation = screenOrientation;
  else
    mCaptureOrientation = landscape ? Qt::LandscapeOrientation : Qt::PortraitOrientation;
  mCaptureOrientationRecorded = true;
}

void CameraOrientationNormalizer::recordCaptureDeviceOrientation( int deviceOrientation, bool viewportLandscape )
{
  switch ( static_cast<QOrientationReading::Orientation>( deviceOrientation ) )
  {
    case QOrientationReading::TopUp:
      mCaptureOrientation = Qt::PortraitOrientation;
      break;
    case QOrientationReading::TopDown:
      mCaptureOrientation = Qt::InvertedPortraitOrientation;
      break;
    case QOrientationReading::LeftUp:
      mCaptureOrientation = Qt::LandscapeOrientation;
      break;
    case QOrientationReading::RightUp:
      mCaptureOrientation = Qt::InvertedLandscapeOrientation;
      break;
    case QOrientationReading::Undefined:
    case QOrientationReading::FaceUp:
    case QOrientationReading::FaceDown:
      recordCaptureViewportOrientation( viewportLandscape );
      return;
  }

  mCaptureOrientationRecorded = true;
}

bool CameraOrientationNormalizer::normalizeImageOrientation( const QString &path )
{
#if defined( Q_OS_ANDROID ) || defined( Q_OS_IOS ) || defined( Q_OS_WIN )
  if ( path.isEmpty() )
  {
    return false;
  }

  // Only a normalizer which observed the shutter event may infer orientation
  // from width/height. This keeps a second validation pass from rotating an
  // already-normalized landscape photo toward the default portrait state.
  const bool captureOrientationRecorded = mCaptureOrientationRecorded;
  mCaptureOrientationRecorded = false;

  return prepareCapturedImage( path, mCaptureOrientation, captureOrientationRecorded );
#else
  Q_UNUSED( path )
  return true;
#endif
}

void CameraOrientationNormalizer::handleScreenOrientationChanged( Qt::ScreenOrientation orientation )
{
  if ( mCurrentOrientation == orientation )
  {
    return;
  }

  mCurrentOrientation = orientation;
  updatePreviewRotation();
}

void CameraOrientationNormalizer::updatePreviewRotation()
{
#if defined( Q_OS_IOS ) || defined( Q_OS_WIN )
  const QScreen *screen = QGuiApplication::primaryScreen();
  if ( !screen )
  {
    return;
  }

  const int screenAngle = screen->angleBetween( screen->nativeOrientation(), mCurrentOrientation );
  const bool isLandscape = ( screenAngle == 90 || screenAngle == 270 );
  const int rotation = isLandscape ? 180 : 0;

  if ( rotation != mPreviewRotation )
  {
    mPreviewRotation = rotation;
    emit previewRotationChanged();
  }
#endif
}
