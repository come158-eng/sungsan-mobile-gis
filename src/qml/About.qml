// Modified for Meta Engineering GIS by Sungsan on 2026-08-11.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.qfield
import Theme

/**
 * \ingroup qml
 */
Item {
  id: aboutPanel

  visible: false
  focus: visible

  Rectangle {
    color: "black"
    opacity: 0.8
    anchors.fill: parent
  }

  ColumnLayout {
    id: aboutContainer
    spacing: 6
    anchors.fill: parent
    anchors.margins: 20
    anchors.topMargin: 20 + mainWindow.sceneTopMargin
    anchors.bottomMargin: 20 + mainWindow.sceneBottomMargin

    ScrollView {
      Layout.fillWidth: true
      Layout.fillHeight: true
      ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
      ScrollBar.vertical: QfScrollBar {}
      contentItem: information
      contentWidth: information.width
      contentHeight: information.height
      clip: true

      MouseArea {
        anchors.fill: parent
        onClicked: aboutPanel.visible = false
      }

      ColumnLayout {
        id: information
        spacing: 6
        width: aboutPanel.width - 40
        height: Math.max(mainWindow.height - sponsorshipButton.height - linksButton.height - qfieldAppDirectoryLabel.height - aboutContainer.spacing * 3 - aboutContainer.anchors.topMargin - aboutContainer.anchors.bottomMargin - 10, qfieldPart.height + (appIsSungsan ? sungsanOpenSourcePart.height : opengisPart.height) + spacing)

        ColumnLayout {
          id: qfieldPart
          Layout.fillWidth: true
          Layout.fillHeight: true

          MouseArea {
            Layout.preferredWidth: 138
            Layout.preferredHeight: 138
            Layout.alignment: Qt.AlignHCenter
            enabled: !appIsSungsan
            Image {
              id: qfieldLogo
              width: parent.width
              height: parent.height
              fillMode: Image.PreserveAspectFit
              source: "qrc:/images/app_logo.svg"
              sourceSize.width: width * screen.devicePixelRatio
              sourceSize.height: height * screen.devicePixelRatio
            }
            onClicked: {
              if (!appIsSungsan) {
                Qt.openUrlExternally("https://qfield.org/");
              }
            }
          }

          Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignCenter
            horizontalAlignment: Text.AlignHCenter
            font: Theme.strongFont
            color: Theme.light
            textFormat: Text.RichText
            wrapMode: Text.WordWrap

            text: {
              if (appIsSungsan) {
                const dependencies = [["QGIS", qgisVersion.split("-", 1)[0]], ["GDAL/OGR", gdalVersion], ["Qt", qVersion]];
                return appName + "<br>" + appVersionStr + "<br>" + dependencies.map(pair => pair.join(" ")).join(" | ");
              }

              let links = '<a href="https://github.com/opengisch/QField/commit/' + gitRev + '">' + gitRev.substr(0, 7) + '</a>';
              if (appVersion && appVersion !== '1.0.0' && appVersion !== '0') {
                links += ' <a href="https://github.com/opengisch/QField/releases/tag/' + appVersion + '">' + appVersion + '</a>';
              }

              let title = appName;
              if (appName === "QField") {
                title += "<br>" + appVersionStr + " (" + links + ")";
              } else {
                title += "<br>" + qsTr("Powered by QField") + " (" + links + ")";
              }

              // the `qgisVersion` has the format `<int>.<int>.<int>-<any text>`, so we get everything before the first `-`
              const dependencies = [["QGIS", qgisVersion.split("-", 1)[0]], ["GDAL/OGR", gdalVersion], ["Qt", qVersion]];
              return title + "<br>" + dependencies.map(pair => pair.join(" ")).join(" | ");
            }

            onLinkActivated: link => Qt.openUrlExternally(link)
          }
        }

        ColumnLayout {
          id: opengisPart
          visible: !appIsSungsan
          Layout.fillWidth: true
          Layout.fillHeight: true

          MouseArea {
            Layout.preferredWidth: 91
            Layout.preferredHeight: 113
            Layout.alignment: Qt.AlignHCenter
            Image {
              id: opengisLogo
              width: parent.width
              height: parent.height
              fillMode: Image.PreserveAspectFit
              source: "qrc:/images/opengis-logo.svg"
              sourceSize.width: width * screen.devicePixelRatio
              sourceSize.height: height * screen.devicePixelRatio
            }
            onClicked: Qt.openUrlExternally("https://opengis.ch")
          }

          Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignCenter
            horizontalAlignment: Text.AlignHCenter
            font: Theme.strongFont
            color: Theme.light
            textFormat: Text.RichText
            text: qsTr("Developed by") + '<br><a href="https://opengis.ch">OPENGIS.ch</a>'
            onLinkActivated: link => Qt.openUrlExternally(link)
          }
        }

        ColumnLayout {
          id: sungsanOpenSourcePart
          visible: appIsSungsan
          Layout.fillWidth: true
          Layout.fillHeight: true
          spacing: 10

          Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignCenter
            horizontalAlignment: Text.AlignHCenter
            font: Theme.strongFont
            color: Theme.light
            wrapMode: Text.WordWrap
            text: "메타이엔지 현장조사 전용 모바일 GIS"
          }

          Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignCenter
            horizontalAlignment: Text.AlignHCenter
            font: Theme.tipFont
            color: Theme.secondaryTextColor
            textFormat: Text.RichText
            wrapMode: Text.WordWrap
            text: {
              const sourceLink = '<a href="https://github.com/opengisch/QField/commit/' + gitRev + '">' + gitRev.substr(0, 7) + '</a>';
              return "<b>오픈소스 정보</b><br>QField 오픈소스 엔진과 QGIS 라이브러리를 사용합니다. " +
                     "QField 기여자와 OPENGIS.ch의 원저작권 및 GNU GPL v2 이상 라이선스는 그대로 유지됩니다.<br>" +
                     "QField 원본 커밋: " + sourceLink;
            }
            onLinkActivated: link => Qt.openUrlExternally(link)
          }
        }
      }
    }

    Label {
      id: qfieldAppDirectoryLabel
      Layout.fillWidth: true
      Layout.maximumWidth: parent.width
      Layout.alignment: Qt.AlignCenter
      Layout.bottomMargin: 10
      horizontalAlignment: Text.AlignHCenter
      font: Theme.tinyFont
      color: Theme.secondaryTextColor
      textFormat: Text.RichText
      wrapMode: Text.WordWrap

      text: {
        let label = '';
        let isDesktopPlatform = Qt.platform.os !== "ios" && Qt.platform.os !== "android";
        let dataDirs = platformUtilities.appDataDirs();
        if (dataDirs.length > 0) {
          label = dataDirs.length > 1 ? qsTr('%1 app directories').arg(appName) : qsTr('%1 app directory').arg(appName);
          for (let dataDir of dataDirs) {
            if (isDesktopPlatform) {
              label += '<br><a href="' + UrlUtils.fromString(dataDir) + '">' + dataDir + '</a>';
            } else {
              label += '<br>' + dataDir;
            }
          }
        }
        return label;
      }

      onLinkActivated: link => Qt.openUrlExternally(link)
    }

    QfButton {
      id: sponsorshipButton
      Layout.fillWidth: true
      icon.source: Theme.getThemeVectorIcon('ic_sponsor_white_24dp')
      enabled: appName === "QField"
      visible: !appIsSungsan && enabled

      text: qsTr('Support QField')
      onClicked: Qt.openUrlExternally("https://github.com/sponsors/opengisch")
    }

    QfButton {
      id: linksButton
      visible: !appIsSungsan
      dropdown: appName === "QField"
      Layout.fillWidth: true
      icon.source: Theme.getThemeVectorIcon('ic_book_white_24dp')

      text: qsTr('Documentation')

      onClicked: {
        Qt.openUrlExternally("https://docs.qfield.org/");
      }

      onDropdownClicked: {
        linksMenu.popup(linksButton.width - linksMenu.width + 10, linksButton.y + 10);
      }
    }
  }

  QfMenu {
    id: linksMenu
    title: qsTr("Links Menu")

    MenuItem {
      text: qsTr('Changelog')

      font: Theme.defaultFont
      height: 48
      leftPadding: Theme.menuItemLeftPadding
      icon.source: Theme.getThemeVectorIcon('ic_speaker_white_24dp')

      onTriggered: {
        changelogPopup.open();
      }
    }
  }

  Keys.onReleased: event => {
    if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
      event.accepted = true;
      visible = false;
    }
  }
}
