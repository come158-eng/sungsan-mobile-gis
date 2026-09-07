/*
 * Meta Engineering GIS home screen
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.qfield
import Theme

Page {
  id: root
  objectName: "sungsanHomeScreen"

  property var model
  property bool hasCurrentProject: false
  property string currentProjectName: ""
  property bool projectImportAvailable: true
  property string pendingLandStarProjectPath: ""

  signal importProjectRequested
  signal browseProjectsRequested
  signal createFieldProjectRequested(string regionName, string siteName, string workDate)
  signal deleteRecentProjectRequested(string path, int projectType, string title)
  signal openRecentProjectRequested(string path, string title, int projectType)
  signal continueSurveyRequested
  signal settingsRequested
  signal openSourceInformationRequested
  signal closeApplicationRequested

  function openProjectCreationDialog(regionName, siteName, workDate, importPath) {
    projectCreationDialog.openWithDefaults(regionName, siteName, workDate, importPath);
  }

  visible: false
  focus: visible

  background: Rectangle {
    color: Theme.mainBackgroundColor
  }

  Rectangle {
    id: brandBand
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: Math.max(180, Math.min(224, root.height * 0.25))
    color: Theme.mainColor

    Rectangle {
      width: 270
      height: 270
      radius: width / 2
      anchors.right: parent.right
      anchors.rightMargin: -105
      anchors.top: parent.top
      anchors.topMargin: -115
      color: Qt.lighter(Theme.mainColor, 1.16)
      opacity: 0.7
    }

    ColumnLayout {
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.top: parent.top
      anchors.leftMargin: 24 + mainWindow.sceneLeftMargin
      anchors.rightMargin: 24 + mainWindow.sceneRightMargin
      anchors.topMargin: 22 + mainWindow.sceneTopMargin
      spacing: 4

      Label {
        text: "메타이엔지"
        color: "#ffffff"
        font.pixelSize: 17
        font.bold: true
        font.letterSpacing: 1
        Layout.bottomMargin: 10
      }

      Label {
        Layout.fillWidth: true
        text: appName
        color: "#ffffff"
        font.pixelSize: 25
        minimumPixelSize: 18
        fontSizeMode: Text.Fit
        font.bold: true
      }

      Label {
        text: "QGIS에서 준비한 지도를 현장에서 그대로 조사합니다"
        color: "#ffe0da"
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        Layout.maximumWidth: root.width - 48
      }
    }
  }

  ScrollView {
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: brandBand.bottom
    anchors.bottom: parent.bottom
    anchors.leftMargin: mainWindow.sceneLeftMargin
    anchors.rightMargin: mainWindow.sceneRightMargin
    clip: true
    contentWidth: availableWidth

    ColumnLayout {
      width: Math.min(584, root.width - mainWindow.sceneLeftMargin - mainWindow.sceneRightMargin - 36)
      anchors.horizontalCenter: parent.horizontalCenter
      anchors.top: parent.top
      anchors.topMargin: 18
      spacing: 14

      Rectangle {
        visible: root.hasCurrentProject
        Layout.fillWidth: true
        Layout.preferredHeight: visible ? 106 : 0
        radius: 18
        color: Theme.controlBackgroundColor
        border.color: Theme.controlBorderColor

        RowLayout {
          anchors.fill: parent
          anchors.margins: 14
          spacing: 12

          Rectangle {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: 14
            color: Theme.controlBackgroundAlternateColor

            Label {
              anchors.centerIn: parent
              text: "▣"
              color: Theme.mainColor
              font.pixelSize: 24
              font.bold: true
            }
          }

          ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Label {
              text: "현재 프로젝트"
              color: Theme.secondaryTextColor
              font.pixelSize: 12
            }
            Label {
              Layout.fillWidth: true
              text: root.currentProjectName.length > 0 ? root.currentProjectName : "열린 프로젝트"
              color: Theme.mainTextColor
              font.pixelSize: 17
              font.bold: true
              elide: Text.ElideMiddle
            }
          }

          Button {
            text: "조사 계속"
            font.bold: true
            onClicked: root.continueSurveyRequested()
            background: Rectangle {
              radius: 13
              color: parent.down ? Qt.darker(Theme.mainColor, 1.2) : Theme.mainColor
            }
            contentItem: Label {
              text: parent.text
              color: "white"
              horizontalAlignment: Text.AlignHCenter
              verticalAlignment: Text.AlignVCenter
              font: parent.font
            }
          }
        }
      }

      Label {
        text: "프로젝트 열기"
        color: Theme.mainTextColor
        font.pixelSize: 17
        font.bold: true
        Layout.topMargin: 2
      }

      GridLayout {
        Layout.fillWidth: true
        columns: root.width >= 520 ? 2 : 1
        columnSpacing: 10
        rowSpacing: 10

        SungsanActionButton {
          Layout.fillWidth: true
          text: "프로젝트 가져오기"
          detailText: "PC에서 만든 QGIS/QField ZIP을 선택"
          iconSource: Theme.getThemeVectorIcon("ic_download_white_24dp")
          emphasized: true
          enabled: root.projectImportAvailable
          onClicked: root.importProjectRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          text: "기기에 있는 프로젝트 열기"
          detailText: "QGIS에서 만든 프로젝트와 자료 찾기"
          iconSource: Theme.getThemeVectorIcon("ic_folder_open_black_24dp")
          onClicked: root.browseProjectsRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.columnSpan: root.width >= 520 ? 2 : 1
          text: "기본 현장 프로젝트 만들기"
          detailText: "LandStar 측점과 현장 사진을 바로 기록"
          iconSource: Theme.getThemeVectorIcon("ic_add_white_24dp")
          onClicked: projectCreationDialog.open()
        }
      }

      RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 5

        Label {
          text: "최근 사용한 현장"
          color: Theme.mainTextColor
          font.pixelSize: 17
          font.bold: true
          Layout.fillWidth: true
        }

        Label {
          visible: recentProjects.count === 0
          text: "아직 없음"
          color: Theme.secondaryTextColor
          font.pixelSize: 12
        }
      }

      ListView {
        id: recentProjects
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 282)
        visible: count > 0
        clip: true
        spacing: 8
        model: root.model
        interactive: contentHeight > height

        delegate: Item {
          id: recentButton
          property string projectPath: ProjectPath
          property string projectTitle: ProjectTitle
          property int projectType: ProjectType
          width: recentProjects.width
          height: 72

          MouseArea {
            id: recentButtonMouseArea
            anchors.fill: parent
            onClicked: root.openRecentProjectRequested(projectPath, projectTitle, projectType)
          }

          Rectangle {
            anchors.fill: parent
            radius: 15
            color: recentButtonMouseArea.pressed ? Theme.controlBackgroundAlternateColor : Theme.controlBackgroundColor
            border.color: Theme.controlBorderColor
          }

          RowLayout {
            spacing: 12
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            Rectangle {
              Layout.preferredWidth: 40
              Layout.preferredHeight: 40
              radius: 12
              color: Theme.controlBackgroundAlternateColor
              Label {
                anchors.centerIn: parent
                text: recentButton.projectType === RecentProjectListModel.LinkProject ? "↓" : "▣"
                color: Theme.mainColor
                font.pixelSize: 22
                font.bold: true
              }
            }
            ColumnLayout {
              Layout.fillWidth: true
              spacing: 2
              Label {
                Layout.fillWidth: true
                text: recentButton.projectTitle.length > 0 ? recentButton.projectTitle : "이름 없는 프로젝트"
                color: Theme.mainTextColor
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
              }
              Label {
                Layout.fillWidth: true
                text: recentButton.projectType === RecentProjectListModel.LinkProject ? "온라인 패키지" : recentButton.projectPath
                color: Theme.secondaryTextColor
                font.pixelSize: 11
                elide: Text.ElideMiddle
              }
            }
            Label {
              text: "열기  ›"
              color: Theme.mainColor
              font.pixelSize: 13
              font.bold: true
            }
            Button {
              id: deleteRecentButton
              Layout.preferredWidth: 44
              Layout.preferredHeight: 44
              icon.source: Theme.getThemeVectorIcon("ic_delete_forever_white_24dp")
              text: ""
              display: AbstractButton.TextOnly
              highlighted: false
              flat: true
              focusPolicy: Qt.NoFocus
              onClicked: {
                root.deleteRecentProjectRequested(projectPath, projectType, projectTitle);
              }
              background: Rectangle {
                anchors.fill: parent
                color: deleteRecentButton.pressed ? "#fff0f0" : "transparent"
                radius: 12
              }

              contentItem: Item {
                Image {
                  anchors.centerIn: parent
                  width: 20
                  height: 20
                  source: deleteRecentButton.icon.source
                }
              }
              Layout.alignment: Qt.AlignVCenter
            }
          }
        }
      }

      Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: "프로젝트를 열면 심볼·라벨·입력 양식이 그대로 적용됩니다."
        color: Theme.secondaryTextColor
        font.pixelSize: 12
        wrapMode: Text.WordWrap
      }

      RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 2
        spacing: 8

        Button {
          Layout.fillWidth: true
          text: "장치·조사 설정"
          flat: true
          onClicked: root.settingsRequested()
          contentItem: Label {
            text: parent.text
            color: Theme.mainColor
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
        }

        Rectangle {
          Layout.preferredWidth: 1
          Layout.preferredHeight: 20
          color: Theme.controlBorderColor
        }

        Button {
          Layout.fillWidth: true
          text: "오픈소스 정보"
          flat: true
          onClicked: root.openSourceInformationRequested()
          contentItem: Label {
            text: parent.text
            color: Theme.secondaryTextColor
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
        }
      }

      Item {
        Layout.preferredHeight: 24 + mainWindow.sceneBottomMargin
      }
    }
  }

  Button {
    anchors.top: parent.top
    anchors.right: parent.right
    anchors.topMargin: 10 + mainWindow.sceneTopMargin
    anchors.rightMargin: 12 + mainWindow.sceneRightMargin
    width: 44
    height: 44
    text: "×"
    flat: true
    font.pixelSize: 26
    onClicked: root.closeApplicationRequested()
    contentItem: Label {
      text: parent.text
      color: "white"
      horizontalAlignment: Text.AlignHCenter
      verticalAlignment: Text.AlignVCenter
      font: parent.font
    }
  }
  Dialog {
    id: projectCreationDialog
    modal: true
    focus: true
    anchors.centerIn: parent
    title: "기본 현장 프로젝트 만들기"
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: Math.min(420, parent.width - 48)
    x: Math.max(mainWindow.sceneLeftMargin, (parent.width - width) / 2)
    y: Math.max(mainWindow.sceneTopMargin, (parent.height - height) / 2)

    property string defaultRegionLabel: "경상남도"
    property string defaultSiteLabel: "메타이엔지 기본 현장"
    property string siteName: ""
    property string regionName: ""
    property string surveyDate: ""

    function openWithDefaults(regionName, siteName, workDate, importPath) {
      this.regionName = regionName || "";
      this.siteName = siteName || "";
      this.surveyDate = workDate || "";
      root.pendingLandStarProjectPath = importPath || "";
      regionNameField.text = this.regionName;
      projectTitleField.text = this.siteName;
      projectDateField.text = this.surveyDate;
      open();
    }

    onAccepted: {
      const safe = v => v.replace(/[\\\/:*?\x22<>|]/g, "_").trim();
      const rawRegion = safe(regionName.trim().length > 0 ? regionName.trim() : defaultRegionLabel);
      const rawSite = safe(siteName.trim().length > 0 ? siteName.trim() : defaultSiteLabel);
      const now = new Date();
      const safeDate = safe(surveyDate.trim().length > 0
        ? surveyDate.trim()
        : `${now.getFullYear()}${String(now.getMonth() + 1).padStart(2, "0")}${String(now.getDate()).padStart(2, "0")}`);
      root.createFieldProjectRequested(rawRegion, rawSite, safeDate);
      // pending path remains for import flow; it will be consumed by the
      // main app when the created project finishes loading.
      siteName = "";
      regionName = "";
      surveyDate = "";
    }

    onRejected: {
      root.pendingLandStarProjectPath = "";
      siteName = "";
      regionName = "";
      surveyDate = "";
    }

    ColumnLayout {
      anchors.fill: parent
      anchors.margins: 16
      spacing: 10

      Label {
        Layout.fillWidth: true
        text: "지역명/현장명을 입력해 주세요."
        wrapMode: Text.WordWrap
        color: Theme.mainTextColor
        font.pixelSize: 15
      }

      Label {
        Layout.fillWidth: true
        text: "예시: 경상남도 / 창녕군 OOO 현장 / 20260821"
        color: Theme.secondaryTextColor
        font.pixelSize: 12
      }

      TextField {
        id: regionNameField
        Layout.fillWidth: true
        text: projectCreationDialog.regionName
        placeholderText: "지역명 (예: 경상남도 창녕군)"
        inputMethodHints: Qt.ImhPreferUppercase | Qt.ImhNoPredictiveText
        onTextChanged: projectCreationDialog.regionName = text
      }

      TextField {
        id: projectTitleField
        Layout.fillWidth: true
        text: projectCreationDialog.siteName
        placeholderText: projectCreationDialog.defaultSiteLabel
        inputMethodHints: Qt.ImhPreferUppercase | Qt.ImhNoPredictiveText
        onTextChanged: projectCreationDialog.siteName = text
      }

      TextField {
        id: projectDateField
        Layout.fillWidth: true
        text: projectCreationDialog.surveyDate
        placeholderText: "측량일 YYYYMMDD · 기본값은 오늘"
        inputMethodHints: Qt.ImhDigitsOnly
        onTextChanged: projectCreationDialog.surveyDate = text
      }
    }
  }

}
