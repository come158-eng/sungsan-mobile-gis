#include "theme.h"
#include <QApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

int main( int argc, char **argv )
{
  QApplication app( argc, argv );
  app.setOrganizationName( QStringLiteral( "MetaThemeTest" ) );
  app.setApplicationName( QStringLiteral( "AppearanceRegression" ) );
  QTemporaryDir settingsDir;
  if ( !settingsDir.isValid() )
    return 1;
  QSettings::setDefaultFormat( QSettings::IniFormat );
  QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, settingsDir.path() );
  const bool meta = app.arguments().value( 1 ) == QStringLiteral( "metaengi" );
  auto check = []( bool condition, const char *message ) {
    if ( !condition )
      qFatal( "%s", message );
  };
  for ( const auto &saved : { "dark", "system", "light" } )
  {
    QSettings().setValue( QStringLiteral( "appearance" ), QString::fromLatin1( saved ) );
    Theme theme;
    check( theme.appearanceLocked() == meta, "Only Meta locks the appearance" );
    if ( meta )
      check( theme.appearance() == QStringLiteral( "light" ) && !theme.darkTheme(), "Saved dark/system preference must migrate to light" );
    theme.setAppearance( QStringLiteral( "dark" ) );
    check( theme.darkTheme() != meta, "Dark setting must not darken Meta" );
    theme.setAppearance( QStringLiteral( "system" ) );
    check( theme.darkTheme() != meta, "Dark OS appearance must not darken Meta" );
    theme.applyAppearance( {}, Theme::DarkAppearance );
    check( theme.darkTheme() != meta, "Explicit dark appearance must honor brand policy" );
    theme.setDarkTheme( true );
    check( theme.darkTheme() != meta, "Direct dark setter must honor brand policy" );
    if ( meta )
    {
      check( theme.mainColor() == QColor( "#c52d26" ), "Meta accent must remain red" );
      check( theme.mainBackgroundColor() == QColor( "#fff7f5" ), "Meta background must remain light" );
      check( theme.controlBackgroundColor() == QColor( "#ffffff" ), "Secondary actions must remain white" );
      check( QSettings().value( "appearance" ).toString() == QStringLiteral( "light" ), "Persisted appearance must remain light" );
    }
    theme.setAppearance( QStringLiteral( "light" ) );
    check( !theme.darkTheme(), "Light appearance must still work for both brands" );
  }
  qInfo() << "PASS: production Theme saved/system/explicit appearance regression" << ( meta ? "Meta" : "Sungsan" );
}
