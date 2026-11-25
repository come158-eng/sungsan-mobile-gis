/***************************************************************************
  positioning.cpp - Positioning

 ---------------------
 begin                : 22.05.2022
 copyright            : (C) 2022 by Mathieu Pellerin
 email                : mathieu at opengis dot ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "filereceiver.h"
#include "platformutilities.h"
#include "positioning.h"
#include "positioningutils.h"
#include "tcpreceiver.h"
#include "udpreceiver.h"
#ifdef WITH_SERIALPORT
#include "serialportreceiver.h"
#endif

#include <QFile>
#include <QGuiApplication>
#include <QPermissions>
#include <QRemoteObjectPendingCall>
#include <QScreen>
#include <qgsapplication.h>
#include <qgsunittypes.h>

Positioning::Positioning( QObject *parent )
  : QObject( parent )
{
  if ( QFile::exists( PositioningSource::backgroundFilePath ) )
  {
    QFile::remove( PositioningSource::backgroundFilePath );
    mProperties["backgroundMode"] = false;
  }

  connect( QgsApplication::instance(), &QGuiApplication::applicationStateChanged, this, &Positioning::onApplicationStateChanged );
}

void Positioning::clearSource()
{
  mSourceEndpoint = nullptr;
  mPositioningSourceReplica.reset();
  mNode.reset();
  if ( mPositioningSource )
  {
    disconnect( mPositioningSource, nullptr, this, nullptr );
    disconnect( this, nullptr, mPositioningSource, nullptr );
    mPositioningSource->setActive( false );
    mPositioningSource->deleteLater();
    mPositioningSource = nullptr;
  }
}

void Positioning::setupSource()
{
  clearSource();
  if ( mServiceMode && ( PlatformUtilities::instance()->capabilities() & PlatformUtilities::PositioningService ) )
  {
    const QUrl url( PlatformUtilities::instance()->startPositioningService() );
    mNode.reset( new QRemoteObjectNode( this ) );
    if ( !mNode->connectToNode( url ) )
    {
      qWarning() << "Could not connect to the positioning service" << mNode->lastError();
      return;
    }
    mPositioningSourceReplica.reset( mNode->acquireDynamic( "PositioningSource" ) );
    if ( mPositioningSourceReplica->isInitialized() )
      connectSource( mPositioningSourceReplica.data() );
    else
      connect( mPositioningSourceReplica.data(), &QRemoteObjectReplica::initialized, this, [this]() {
        if ( !mSourceEndpoint )
          connectSource( mPositioningSourceReplica.data() );
      } );
  }
  else
  {
    PlatformUtilities::instance()->stopPositioningService();
    // Foreground positioning runs on this thread. Only a service in another process needs IPC.
    mPositioningSource = new PositioningSource( this );
    connectSource( mPositioningSource );
  }
}

void Positioning::connectSource( QObject *source )
{
  mSourceEndpoint = source;
  connect( source, SIGNAL( activeChanged() ), this, SLOT( onActiveChanged() ) );
  connect( source, SIGNAL( validChanged() ), this, SLOT( onValidChanged() ) );
  connect( source, SIGNAL( deviceIdChanged() ), this, SLOT( onDeviceIdChanged() ) );
  connect( source, SIGNAL( deviceChanged() ), this, SIGNAL( deviceChanged() ) );
  connect( source, SIGNAL( elevationCorrectionModeChanged() ), this, SLOT( onElevationCorrectionModeChanged() ) );
  connect( source, SIGNAL( antennaHeightChanged() ), this, SLOT( onAntennaHeightChanged() ) );
  connect( source, SIGNAL( loggingChanged() ), this, SLOT( onLoggingChanged() ) );
  connect( source, SIGNAL( loggingPathChanged() ), this, SLOT( onLoggingPathChanged() ) );
  connect( source, SIGNAL( positionInformationChanged() ), this, SLOT( onPositionInformationChanged() ) );

  connect( source, SIGNAL( deviceLastErrorChanged() ), this, SIGNAL( deviceLastErrorChanged() ) );
  connect( source, SIGNAL( deviceSocketStateChanged() ), this, SIGNAL( deviceSocketStateChanged() ) );
  connect( source, SIGNAL( deviceSocketStateStringChanged() ), this, SIGNAL( deviceSocketStateStringChanged() ) );
  connect( source, SIGNAL( orientationChanged() ), this, SIGNAL( orientationChanged() ) );

  connect( source, SIGNAL( enableNtripChanged() ), this, SIGNAL( enableNtripChanged() ) );
  connect( source, SIGNAL( ntripSettingsChanged() ), this, SIGNAL( ntripSettingsChanged() ) );
  connect( source, SIGNAL( ntripStateChanged() ), this, SIGNAL( ntripStateChanged() ) );
  connect( source, SIGNAL( ntripBytesSentChanged() ), this, SIGNAL( ntripBytesSentChanged() ) );
  connect( source, SIGNAL( ntripBytesReceivedChanged() ), this, SIGNAL( ntripBytesReceivedChanged() ) );
  connect( source, SIGNAL( ntripLastBytesReceivedUtcDateTimeChanged() ), this, SIGNAL( ntripLastBytesReceivedUtcDateTimeChanged() ) );

  connect( this, SIGNAL( triggerConnectDevice() ), source, SLOT( triggerConnectDevice() ) );
  connect( this, SIGNAL( triggerDisconnectDevice() ), source, SLOT( triggerDisconnectDevice() ) );

  // Synchronize properties
  const QList<QString> properties = mProperties.keys();
  for ( const QString &property : properties )
  {
    if ( property != QStringLiteral( "active" ) )
    {
      mSourceEndpoint->setProperty( property.toLatin1(), mProperties[property] );
    }
  }

  // Give the OS 2 seconds to fully release the Bluetooth adapter from the dying local/remote source
  if ( mProperties.contains( "active" ) )
  {
    QTimer::singleShot( 2000, source, [this, source]() {
      if ( mSourceEndpoint == source && mProperties.contains( "active" ) )
      {
        const bool actualActiveValue = mProperties["active"].toBool();
        mProperties.remove( "active" );
        mSourceEndpoint->setProperty( "active", actualActiveValue );
      }
    } );
  }
  onValidChanged();
}

bool Positioning::isSourceAvailable() const
{
  return mSourceEndpoint && ( !mPositioningSourceReplica || mPositioningSourceReplica->isInitialized() );
}

void Positioning::onApplicationStateChanged( Qt::ApplicationState state )
{
#if defined( Q_OS_ANDROID ) || defined( Q_OS_IOS )
  // Google Play policy only allows for background access if it's explicitly stated and justified
  // Not stopping on Activity::onPause is detected as violation
  if ( mServiceMode )
  {
    // Service path
    setBackgroundMode( state != Qt::ApplicationState::ApplicationActive );
  }
  else
  {
    // Non-service path
    const bool isActive = active();
    if ( isActive )
    {
      switch ( state )
      {
        case Qt::ApplicationState::ApplicationActive:
          if ( isActive )
          {
            emit triggerConnectDevice();
          }
          break;

        default:
          if ( isActive )
          {
            emit triggerDisconnectDevice();
          }
      }
    }
  }
#else
  Q_UNUSED( state )
#endif
}

void Positioning::onActiveChanged()
{
  if ( mProperties["active"] == mSourceEndpoint->property( "active" ) )
  {
    return;
  }

  mProperties["active"] = mSourceEndpoint->property( "active" );
  emit activeChanged();
}

bool Positioning::active() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "active" ).toBool() : false;
}

void Positioning::setActive( bool active )
{
  const QString devId = deviceId();
  if ( !active )
    ++mPermissionRequestGeneration;
  // Permission prompts are only required when starting a receiver. Asking
  // while stopping can asynchronously turn it back on from the callback,
  // making an internal/external switch race with itself.
  if ( active && devId.isEmpty() )
  {
    // Handle internal receiver permission
    if ( !mInternalPermissionChecked )
    {
      QLocationPermission locationPermission;
      locationPermission.setAccuracy( QLocationPermission::Precise );
      Qt::PermissionStatus permissionStatus = qApp->checkPermission( locationPermission );

      if ( permissionStatus == Qt::PermissionStatus::Undetermined )
      {
        qApp->requestPermission( locationPermission, this, [this, devId, generation = mPermissionRequestGeneration]( const QPermission &permission ) {
          if ( generation != mPermissionRequestGeneration || deviceId() != devId )
            return;
          if ( permission.status() == Qt::PermissionStatus::Granted )
          {
            mInternalPermissionChecked = true;
            setActive( true );
          }
          else
          {
            setValid( false );
          }
        } );
        return;
      }
      else if ( permissionStatus == Qt::PermissionStatus::Denied )
      {
        setValid( false );
        return;
      }
      mInternalPermissionChecked = true;
    }
  }
  else if ( active )
  {
    // Handle external receiver permission
    if (
      !devId.startsWith( TcpReceiver::identifier + ":" )
      && !devId.startsWith( UdpReceiver::identifier + ":" )
      && !devId.startsWith( FileReceiver::identifier + ":" )
#ifdef WITH_SERIALPORT
      && !devId.startsWith( SerialPortReceiver::identifier + ":" )
#endif
    )
    {
      if ( !mBluetoothPermissionChecked )
      {
        QBluetoothPermission bluetoothPermission;
        bluetoothPermission.setCommunicationModes( QBluetoothPermission::Access );
        Qt::PermissionStatus permissionStatus = qApp->checkPermission( bluetoothPermission );
        if ( permissionStatus == Qt::PermissionStatus::Undetermined )
        {
          qApp->requestPermission( bluetoothPermission, this, [this, devId, generation = mPermissionRequestGeneration]( const QPermission &permission ) {
            if ( generation != mPermissionRequestGeneration || deviceId() != devId )
              return;
            if ( permission.status() == Qt::PermissionStatus::Granted )
            {
              mBluetoothPermissionChecked = true;
              setActive( true );
            }
            else
            {
              setValid( false );
            }
          } );
          return;
        }
        else if ( permissionStatus == Qt::PermissionStatus::Denied )
        {
          setValid( false );
          return;
        }
        mBluetoothPermissionChecked = true;
      }
    }
  }

  if ( active && !mSourceEndpoint && !mPositioningSourceReplica )
    setupSource();

  if ( !isSourceAvailable() )
  {
    mProperties["active"] = active;
    return;
  }
  if ( mSourceEndpoint->property( "active" ).toBool() != active )
  {
    mSourceEndpoint->setProperty( "active", active );
  }
  else
  {
    mProperties["active"] = active;
    emit activeChanged();
  }
}

void Positioning::onValidChanged()
{
  const bool valid = mSourceEndpoint->property( "valid" ).toBool();
  if ( mValid == valid )
  {
    return;
  }

  mValid = valid;
  emit validChanged();
}

bool Positioning::valid() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "valid" ).toBool() : mValid;
}

void Positioning::setValid( bool valid )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "valid", valid );
  }
  else
  {
    mValid = valid;
    emit validChanged();
  }
}

void Positioning::onDeviceIdChanged()
{
  if ( mProperties["deviceId"] == mSourceEndpoint->property( "deviceId" ) )
  {
    return;
  }

  mProperties["deviceId"] = mSourceEndpoint->property( "deviceId" );
  emit deviceIdChanged();
}

QString Positioning::deviceId() const
{
  return ( isSourceAvailable() ? mSourceEndpoint->property( "deviceId" ) : mProperties.value( "deviceId" ) ).toString();
}

void Positioning::setDeviceId( const QString &id )
{
  if ( id != deviceId() )
    ++mPermissionRequestGeneration;
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "deviceId", id );
  }
  else
  {
    mProperties["deviceId"] = id;
    emit deviceIdChanged();
  }
}

QString Positioning::deviceLastError() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "deviceLastError" ).toString() : QString();
}

QAbstractSocket::SocketState Positioning::deviceSocketState() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "deviceSocketState" ).value<QAbstractSocket::SocketState>() : QAbstractSocket::UnconnectedState;
}

QString Positioning::deviceSocketStateString() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "deviceSocketStateString" ).toString() : QString();
}

double Positioning::deviceBatteryLevel() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "deviceBatteryLevel" ).toDouble() : std::numeric_limits<double>::quiet_NaN();
}

GnssPositionDetails Positioning::deviceDetails() const
{
  GnssPositionDetails list;
  if ( isSourceAvailable() )
  {
    list = mSourceEndpoint->property( "deviceDetails" ).value<GnssPositionDetails>();
  }
  return list;
}

AbstractGnssReceiver::Capabilities Positioning::deviceCapabilities() const
{
  return isSourceAvailable() ? static_cast<AbstractGnssReceiver::Capabilities>( mSourceEndpoint->property( "deviceCapabilities" ).toInt() ) : AbstractGnssReceiver::NoCapabilities;
}

int Positioning::averagedPositionCount() const
{
  return static_cast<int>( mCollectedPositionInformations.size() );
}

bool Positioning::averagedPosition() const
{
  return mAveragedPosition;
}

void Positioning::setAveragedPosition( bool averaged )
{
  if ( mAveragedPosition == averaged )
    return;

  mAveragedPosition = averaged;
  if ( mAveragedPosition )
  {
    mCollectedPositionInformations << mPositionInformation;
  }
  else
  {
    mCollectedPositionInformations.clear();
  }

  emit averagedPositionCountChanged();
  emit averagedPositionChanged();
}

void Positioning::onLoggingChanged()
{
  if ( mProperties["logging"] == mSourceEndpoint->property( "logging" ) )
  {
    return;
  }

  mProperties["logging"] = mSourceEndpoint->property( "logging" );
  emit loggingChanged();
}

bool Positioning::logging() const
{
  return ( isSourceAvailable() ? mSourceEndpoint->property( "logging" ) : mProperties.value( "logging", false ) ).toBool();
}

void Positioning::setLogging( bool logging )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "logging", logging );
  }
  else
  {
    mProperties["logging"] = logging;
    emit loggingChanged();
  }
}

void Positioning::onLoggingPathChanged()
{
  if ( mProperties["loggingPath"] == mSourceEndpoint->property( "loggingPath" ) )
  {
    return;
  }

  mProperties["loggingPath"] = mSourceEndpoint->property( "loggingPath" );
  emit loggingPathChanged();
}

QString Positioning::loggingPath() const
{
  return ( isSourceAvailable() ? mSourceEndpoint->property( "loggingPath" ) : mProperties.value( "loggingPath" ) ).toString();
}

void Positioning::setLoggingPath( const QString &path )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "loggingPath", path );
  }
  else
  {
    mProperties["loggingPath"] = path;
    emit loggingPathChanged();
  }
}

bool Positioning::serviceMode() const
{
  return mServiceMode;
}

void Positioning::setServiceMode( bool enabled )
{
  if ( mServiceMode == enabled )
    return;

  mServiceMode = enabled;

  if ( active() || mProperties.value( "active" ).toBool() )
  {
    setupSource();
  }
  else
  {
    clearSource();
    if ( !enabled )
      PlatformUtilities::instance()->stopPositioningService();
  }

  emit serviceModeChanged();
}

bool Positioning::backgroundMode() const
{
  return mBackgroundMode;
}

void Positioning::setBackgroundMode( bool enabled )
{
  if ( mBackgroundMode == enabled )
    return;

  mBackgroundMode = enabled;

  QFile backgroundFile( PositioningSource::backgroundFilePath );
  if ( mBackgroundMode )
  {
    if ( backgroundFile.open( QFile::WriteOnly ) )
    {
      backgroundFile.close();
    }
  }
  else
  {
    if ( backgroundFile.exists() )
    {
      backgroundFile.remove();
    }
  }

  if ( isSourceAvailable() )
  {
    // Note that on Android, the property will not be set if the application is suspended _until_ it has become active again
    mSourceEndpoint->setProperty( "backgroundMode", mBackgroundMode );
  }

  emit backgroundModeChanged();
}

bool Positioning::enableNtrip() const
{
  return ( isSourceAvailable() ? mSourceEndpoint->property( "enableNtrip" ) : mProperties.value( "enableNtrip", false ) ).toBool();
}

void Positioning::setEnableNtrip( bool enableNtrip )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "enableNtrip", enableNtrip );
  }
  else
  {
    mProperties["enableNtrip"] = enableNtrip;
    emit enableNtripChanged();
  }
}

NtripSettings Positioning::ntripSettings() const
{
  if ( isSourceAvailable() )
  {
    return mSourceEndpoint->property( "ntripSettings" ).value<NtripSettings>();
  }
  else if ( mProperties.contains( "ntripSettings" ) )
  {
    return mProperties.value( "ntripSettings" ).value<NtripSettings>();
  }

  return NtripSettings();
}

void Positioning::setNtripSettings( const NtripSettings &ntripSettings )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "ntripSettings", QVariant::fromValue<NtripSettings>( ntripSettings ) );
  }
  else
  {
    mProperties["ntripSettings"] = QVariant::fromValue<NtripSettings>( ntripSettings );
    emit ntripSettingsChanged();
  }
}

PositioningSource::NtripState Positioning::ntripState() const
{
  return static_cast<PositioningSource::NtripState>( ( isSourceAvailable() ? mSourceEndpoint->property( "ntripState" ).toInt() : static_cast<int>( PositioningSource::NtripState::Disconnected ) ) );
}

qint64 Positioning::ntripBytesSent() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "ntripBytesSent" ).toLongLong() : 0;
}

qint64 Positioning::ntripBytesReceived() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "ntripBytesReceived" ).toLongLong() : 0;
}

QDateTime Positioning::ntripLastBytesReceivedUtcDateTime() const
{
  return isSourceAvailable() ? mSourceEndpoint->property( "ntripLastBytesReceivedUtcDateTime" ).toDateTime() : QDateTime();
}

QList<GnssPositionInformation> Positioning::getBackgroundPositionInformation() const
{
  if ( mPositioningSource )
    return mPositioningSource->getBackgroundPositionInformation();

  QList<GnssPositionInformation> positionInformationList;
  if ( isSourceAvailable() )
  {
    QRemoteObjectPendingCall call;
    QMetaObject::invokeMethod( mSourceEndpoint, "getBackgroundPositionInformation", Qt::DirectConnection, Q_RETURN_ARG( QRemoteObjectPendingCall, call ) );
    call.waitForFinished();
    positionInformationList = call.returnValue().value<QList<GnssPositionInformation>>();
  }

  return positionInformationList;
}

void Positioning::onElevationCorrectionModeChanged()
{
  if ( mProperties["elevationCorrectionMode"] == mSourceEndpoint->property( "elevationCorrectionMode" ) )
  {
    return;
  }

  mProperties["elevationCorrectionMode"] = mSourceEndpoint->property( "elevationCorrectionMode" );
  emit elevationCorrectionModeChanged();
}

PositioningSource::ElevationCorrectionMode Positioning::elevationCorrectionMode() const
{
  return static_cast<PositioningSource::ElevationCorrectionMode>( ( isSourceAvailable() ? mSourceEndpoint->property( "elevationCorrectionMode" ) : mProperties.value( "elevationCorrectionMode", static_cast<int>( PositioningSource::ElevationCorrectionMode::None ) ) ).toInt() );
}

void Positioning::setElevationCorrectionMode( PositioningSource::ElevationCorrectionMode elevationCorrectionMode )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "elevationCorrectionMode", static_cast<int>( elevationCorrectionMode ) );
  }
  else
  {
    mProperties["elevationCorrectionMode"] = static_cast<int>( elevationCorrectionMode );
    emit elevationCorrectionModeChanged();
  }
}

void Positioning::onAntennaHeightChanged()
{
  if ( mProperties["antennaHeight"] == mSourceEndpoint->property( "antennaHeight" ) )
  {
    return;
  }

  mProperties["antennaHeight"] = mSourceEndpoint->property( "antennaHeight" );
  emit antennaHeightChanged();
}

double Positioning::antennaHeight() const
{
  return ( isSourceAvailable() ? mSourceEndpoint->property( "antennaHeight" ) : mProperties.value( "antennaHeight", 0.0 ) ).toDouble();
}

void Positioning::setAntennaHeight( double antennaHeight )
{
  if ( isSourceAvailable() )
  {
    mSourceEndpoint->setProperty( "antennaHeight", antennaHeight );
  }
  else
  {
    mProperties["antennaHeight"] = antennaHeight;
    emit antennaHeightChanged();
  }
}

GnssPositionInformation Positioning::positionInformation() const
{
  return mPositionInformation;
}

double Positioning::orientation() const
{
  return isSourceAvailable() ? adjustOrientation( mSourceEndpoint->property( "orientation" ).toDouble() ) : std::numeric_limits<double>::quiet_NaN();
}

double Positioning::adjustOrientation( double orientation ) const
{
  // Take into account the orientation of the device
  QScreen *screen = QgsApplication::instance()->primaryScreen();
  switch ( screen->orientation() )
  {
    case Qt::LandscapeOrientation:
      orientation += 90;
      break;
    case Qt::InvertedLandscapeOrientation:
      orientation += 270;
      break;
    case Qt::PortraitOrientation:
    default:
      break;
  }

  return std::fmod( orientation, 360 );
}

void Positioning::setCoordinateTransformer( QgsQuickCoordinateTransformer *coordinateTransformer )
{
  if ( mCoordinateTransformer == coordinateTransformer )
    return;

  if ( mCoordinateTransformer )
  {
    disconnect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::destinationCrsChanged, this, &Positioning::processProjectedPosition );
    disconnect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::transformContextChanged, this, &Positioning::processProjectedPosition );
    disconnect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::deltaZChanged, this, &Positioning::processProjectedPosition );
    disconnect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::skipAltitudeTransformationChanged, this, &Positioning::processProjectedPosition );
    disconnect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::verticalGridChanged, this, &Positioning::processProjectedPosition );
  }

  mCoordinateTransformer = coordinateTransformer;

  if ( mCoordinateTransformer )
  {
    connect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::destinationCrsChanged, this, &Positioning::processProjectedPosition );
    connect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::transformContextChanged, this, &Positioning::processProjectedPosition );
    connect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::deltaZChanged, this, &Positioning::processProjectedPosition );
    connect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::skipAltitudeTransformationChanged, this, &Positioning::processProjectedPosition );
    connect( mCoordinateTransformer, &QgsQuickCoordinateTransformer::verticalGridChanged, this, &Positioning::processProjectedPosition );
  }

  emit coordinateTransformerChanged();
}

QgsPoint Positioning::sourcePosition() const
{
  return mSourcePosition;
}

QgsPoint Positioning::projectedPosition() const
{
  return mProjectedPosition;
}

double Positioning::projectedHorizontalAccuracy() const
{
  return mProjectedHorizontalAccuracy;
}

void Positioning::onPositionInformationChanged()
{
  mPositionInformation = mSourceEndpoint->property( "positionInformation" ).value<GnssPositionInformation>();

  GnssPositionInformation::AccuracyQuality quality = GnssPositionInformation::AccuracyQuality::AccuracyBad;
  const double hacc = mPositionInformation.hacc();
  const bool isExcellentThresholdDefined = !std::isnan( excellentAccuracyThreshold() );
  const bool isBadThresholdDefined = !std::isnan( badAccuracyThreshold() );

  if ( isExcellentThresholdDefined && isBadThresholdDefined )
  {
    if ( !std::isnan( hacc ) )
    {
      if ( hacc <= excellentAccuracyThreshold() )
      {
        quality = GnssPositionInformation::AccuracyExcellent;
      }
      else if ( hacc <= badAccuracyThreshold() )
      {
        quality = GnssPositionInformation::AccuracyOk;
      }
      else
      {
        quality = GnssPositionInformation::AccuracyBad;
      }
    }
    else
    {
      quality = GnssPositionInformation::AccuracyBad;
    }
  }
  else
  {
    quality = GnssPositionInformation::AccuracyUndetermined;
  }

  mPositionInformation.setAccuracyQuality( quality );


  if ( mAveragedPosition )
  {
    if ( !mAveragedPositionFilterAccuracy || mPositionInformation.accuracyQuality() != GnssPositionInformation::AccuracyBad )
    {
      mCollectedPositionInformations << mPositionInformation;
    }
    mPositionInformation = PositioningUtils::averagedPositionInformation( mCollectedPositionInformations );
    emit averagedPositionCountChanged();
  }

  if ( mPositionInformation.isValid() )
  {
    mSourcePosition = QgsPoint( mPositionInformation.longitude(), mPositionInformation.latitude(), mPositionInformation.elevation() );
  }
  else
  {
    mSourcePosition.clear();
    mProjectedPosition.clear();
  }

  if ( mPositionInformation.orientationValid() )
  {
    mPositionInformation.setOrientation( adjustOrientation( mPositionInformation.orientation() ) );
  }

  if ( mCoordinateTransformer && !mSourcePosition.isEmpty() )
  {
    // positionInformationChanged() will be emitted in this function
    processProjectedPosition();
  }
  else
  {
    emit positionInformationChanged();
  }
  // Reprojecting a cached position also emits positionInformationChanged.
  // Only a sample from the positioning service renews the field capture clock.
  emit receiverSampleReceived();
}

void Positioning::processProjectedPosition()
{
  if ( !mSourcePosition.isEmpty() )
  {
    mProjectedPosition = mCoordinateTransformer->transformPosition( mSourcePosition );
    mProjectedHorizontalAccuracy = mPositionInformation.hacc();
    if ( mPositionInformation.haccValid() )
    {
      if ( mCoordinateTransformer->destinationCrs().mapUnits() != Qgis::DistanceUnit::Unknown )
      {
        mProjectedHorizontalAccuracy *= QgsUnitTypes::fromUnitToUnitFactor( Qgis::DistanceUnit::Meters,
                                                                            mCoordinateTransformer->destinationCrs().mapUnits() );
      }
      else
      {
        mProjectedHorizontalAccuracy = 0.0;
      }
    }
  }

  emit positionInformationChanged();
}


void Positioning::setBadAccuracyThreshold( double threshold )
{
  if ( mBadAccuracyThreshold == threshold )
    return;

  mBadAccuracyThreshold = threshold;
  emit badAccuracyThresholdChanged();
}

void Positioning::setExcellentAccuracyThreshold( double threshold )
{
  if ( mExcellentAccuracyThreshold == threshold )
    return;

  mExcellentAccuracyThreshold = threshold;
  emit excellentAccuracyThresholdChanged();
}

bool Positioning::averagedPositionFilterAccuracy() const
{
  return mAveragedPositionFilterAccuracy;
}

void Positioning::setAveragedPositionFilterAccuracy( bool enabled )
{
  if ( mAveragedPositionFilterAccuracy == enabled )
    return;

  mAveragedPositionFilterAccuracy = enabled;
  emit averagedPositionFilterAccuracyChanged();
}
