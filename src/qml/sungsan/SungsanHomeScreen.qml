/*
 * Sungsan Mobile GIS home screen
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

  signal importProjectRequested
  signal browseProjectsRequested
  signal createFieldProjectRequested
  signal openRecentProjectRequested(string path, string title, int projectType)
  signal continueSurveyRequested
  signal settingsRequested
  signal openSourceInformationRequested
  signal closeApplicationRequested

  visible: false
  focus: visible

  background: Rectangle {
    color: "#f3f6fa"
  }

  Rectangle {
    id: brandBand
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    height: Math.max(208, root.height * 0.30)
    color: "#194793"

    Rectangle {
      width: 270
      height: 270
      radius: width / 2
      anchors.right: parent.right
      anchors.rightMargin: -105
      anchors.top: parent.top
      anchors.topMargin: -115
      color: "#2557a5"
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

      Rectangle {
        Layout.preferredWidth: 168
        Layout.preferredHeight: 54
        radius: 27
        color: "#ffffff"

        Label {
          anchors.centerIn: parent
          text: "SUNG SAN"
          color: "#194793"
          font.pixelSize: 21
          font.bold: true
          font.letterSpacing: 1.5
        }
      }

      Label {
        text: "성산 모바일 GIS"
        color: "#ffffff"
        font.pixelSize: 25
        font.bold: true
      }

      Label {
        text: "QGIS에서 준비한 지도를 현장에서 그대로 조사합니다"
        color: "#dbe8ff"
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
        color: "#ffffff"
        border.color: "#cfdbea"

        RowLayout {
          anchors.fill: parent
          anchors.margins: 14
          spacing: 12

          Rectangle {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: 14
            color: "#e9f1ff"

            Label {
              anchors.centerIn: parent
              text: "▣"
              color: "#194793"
              font.pixelSize: 24
              font.bold: true
            }
          }

          ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Label {
              text: "현재 현장"
              color: "#64778b"
              font.pixelSize: 12
            }
            Label {
              Layout.fillWidth: true
              text: root.currentProjectName.length > 0 ? root.currentProjectName : "열린 프로젝트"
              color: "#17324d"
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
              color: parent.down ? "#123975" : "#194793"
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
        text: "프로젝트 준비"
        color: "#17324d"
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
          text: "현장 패키지 가져오기"
          detailText: "PC에서 만든 ZIP을 선택"
          iconSource: Theme.getThemeVectorIcon("ic_download_white_24dp")
          emphasized: true
          enabled: root.projectImportAvailable
          onClicked: root.importProjectRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          text: "기기에 있는 프로젝트 열기"
          detailText: "가져온 프로젝트와 자료 찾기"
          iconSource: Theme.getThemeVectorIcon("ic_folder_open_black_24dp")
          onClicked: root.browseProjectsRequested()
        }

        SungsanActionButton {
          Layout.fillWidth: true
          Layout.columnSpan: root.width >= 520 ? 2 : 1
          text: "기본 현장 프로젝트 만들기"
          detailText: "LandStar 측점과 현장 사진을 바로 기록"
          iconSource: Theme.getThemeVectorIcon("ic_add_white_24dp")
          onClicked: root.createFieldProjectRequested()
        }
      }

      RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 5

        Label {
          text: "최근 사용한 현장"
          color: "#17324d"
          font.pixelSize: 17
          font.bold: true
          Layout.fillWidth: true
        }

        Label {
          visible: recentProjects.count === 0
          text: "아직 없음"
          color: "#7a8998"
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

        delegate: Button {
          id: recentButton
          property string projectPath: ProjectPath
          property string projectTitle: ProjectTitle
          property int projectType: ProjectType
          width: recentProjects.width
          height: 72
          leftPadding: 14
          rightPadding: 14

          onClicked: root.openRecentProjectRequested(projectPath, projectTitle, projectType)

          contentItem: RowLayout {
            spacing: 12
            Rectangle {
              Layout.preferredWidth: 40
              Layout.preferredHeight: 40
              radius: 12
              color: "#eef3f9"
              Label {
                anchors.centerIn: parent
                text: recentButton.projectType === RecentProjectListModel.LinkProject ? "↓" : "▣"
                color: "#194793"
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
                color: "#17324d"
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
              }
              Label {
                Layout.fillWidth: true
                text: recentButton.projectType === RecentProjectListModel.LinkProject ? "온라인 패키지" : recentButton.projectPath
                color: "#718293"
                font.pixelSize: 11
                elide: Text.ElideMiddle
              }
            }
            Label {
              text: "열기  ›"
              color: "#194793"
              font.pixelSize: 13
              font.bold: true
            }
          }

          background: Rectangle {
            radius: 15
            color: recentButton.down ? "#e9f0f8" : "#ffffff"
            border.color: "#d9e1e9"
          }
        }
      }

      Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: "프로젝트를 열면 심볼·라벨·입력 양식이 그대로 적용됩니다."
        color: "#6e7e8e"
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
            color: "#194793"
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
        }

        Rectangle {
          Layout.preferredWidth: 1
          Layout.preferredHeight: 20
          color: "#d5dde6"
        }

        Button {
          Layout.fillWidth: true
          text: "오픈소스 정보"
          flat: true
          onClicked: root.openSourceInformationRequested()
          contentItem: Label {
            text: parent.text
            color: "#536b82"
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
}
