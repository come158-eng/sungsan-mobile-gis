// Execute the handlers shipped in the app with controlled map, receiver and timer state.
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const qml = fs.readFileSync(process.argv[2] || path.join(__dirname, "../src/qml/qgismobileapp.qml"), "utf8");

function body(signature, after = 0, source = qml) {
  const start = source.indexOf(signature, after);
  assert.ok(start >= 0, signature);
  const brace = source.indexOf("{", start);
  let depth = 1, end = brace + 1;
  while (depth > 0 && end < source.length) {
    if (source[end] === "{") ++depth;
    if (source[end] === "}") --depth;
    ++end;
  }
  assert.equal(depth, 0, signature);
  return source.slice(brace, end);
}
function handler(ctx, signature, args = "", after = 0) {
  return vm.runInContext("(function(" + args + ")" + body(signature, after) + ")", ctx);
}
function context(props) {
  return vm.createContext({ displayToast() {}, qsTr: (value) => value, ...props });
}
function timer() {
  return { armed: false, restart() { this.armed = true; }, stop() { this.armed = false; } };
}

const state = {}, internal = { positioningDevice: "", positioningDeviceName: "Internal device" };
const receiver = { active: true, deviceId: "", positionInformation: {isValid: true, latitudeValid: true, longitudeValid: true}, projectedPosition: {x: 0, y: 24} };
const switching = timer(), activating = timer();
let mapJumps = 0;
const gps = context({
  mainWindow: state, positioningSettings: internal, positionSource: receiver, active: true, deviceId: "",
  mainWindowSettings: { sungsanExternalPositioningDevice: "bt:test", sungsanExternalPositioningDeviceName: "Receiver" },
  sungsanPositionDeviceSwitchTimer: switching, sungsanPositionDeviceActivateTimer: activating,
  QAbstractSocket: {UnconnectedState: 0}, gnssButton: {jumpToLocation() { ++mapJumps; }},
  qfieldSettings: { reset() {} }
});
const select = handler(gps, "function sungsanShowCurrentLocation", "source");
const runSwitch = handler(gps, "onTriggered:", "", qml.indexOf("id: sungsanPositionDeviceSwitchTimer"));
const runActivate = handler(gps, "onTriggered:", "", qml.indexOf("id: sungsanPositionDeviceActivateTimer"));
const tick = (t, fn) => { if (t.armed) { t.stop(); fn(); } };
select("external");
assert.ok(switching.armed);
select("phone");
tick(switching, runSwitch); tick(activating, runActivate);
assert.equal(internal.positioningDevice, "", "late timer overrode phone selection");
select("external"); tick(switching, runSwitch);
assert.equal(internal.positioningDevice, "bt:test");
select("phone"); tick(activating, runActivate); tick(switching, runSwitch); tick(activating, runActivate);
assert.equal(internal.positioningDevice, "");
assert.equal(state.sungsanPositionSwitchPending, false);
const sample = handler(gps, "onReceiverSampleReceived:");
gps.jumpToPosition = true; sample();
assert.equal(state.sungsanGnssFresh, true); assert.equal(mapJumps, 1, "valid zero easting discarded");
state.sungsanGnssFresh = false; state.sungsanPositionSwitchPending = true; sample();
assert.equal(state.sungsanGnssFresh, false);
state.sungsanPositionSwitchPending = false; gps.deviceId = "old"; sample();
assert.equal(state.sungsanGnssFresh, false);
gps.deviceId = ""; receiver.positionInformation.latitudeValid = false; sample();
assert.equal(state.sungsanGnssFresh, false);
receiver.positionInformation.latitudeValid = true; receiver.positionInformation.isValid = false; sample();
assert.equal(state.sungsanGnssFresh, false, "finite coordinates from an invalid NMEA fix accepted");
console.log("PASS phone/external switch reversal during each timer, valid/invalid/mismatched fixes and zero easting");

const cad = {id: "cad_1", name: "CAD", geometryType: () => 0}, survey = {id: "survey_1", name: "field", geometryType: () => 0};
const roles = {Layer: 1, Type: 2, VectorLayerPointer: 3, ReadOnly: 4, FeatureAdditionLocked: 5, AttributeEditingLocked: 6};
let rows = [cad, survey];
const layers = context({
  FlatLayerTreeModel: roles, Qgis: {GeometryType: {Point: 0, Line: 1, Polygon: 2, Null: 3}},
  activeLayer: survey, projectInfo: {editRights: true, insertRights: true},
  layerTree: {rowCount: () => rows.length, index: x => x, data: (i, role) => {
    const l = rows[i];
    if (l.legendOf) return ({2: 0, 3:l.legendOf, 4:false, 5:false, 6:false})[role];
    return ({2: 1, 3:l, 4:!!l.lock, 5:!!l.addLock, 6:!!l.editLock})[role];
  }}
});
const chooseLayer = handler(layers, "function ensureEditableLayerSelected", "requireFeatureAddition");
const canEdit = handler(layers, "function canEditLayerAttributes", "layer");
const symbol = {legendOf: survey};
rows = [cad, survey, symbol, symbol];
assert.ok(chooseLayer(true).ok && chooseLayer(false).ok && canEdit(survey), "expanded symbols overrode selected layer");
rows = [symbol, cad, survey, symbol];
survey.editLock = true;
assert.ok(!canEdit(survey) && !chooseLayer(false).ok && chooseLayer(true).ok, "symbol allowed locked attributes");
survey.editLock = false;
survey.lock = true;
assert.ok(!canEdit(survey) && !chooseLayer(true).ok && !chooseLayer(false).ok, "symbol allowed read-only parent");
survey.lock = false;
rows = [symbol];
assert.ok(!canEdit(survey) && !chooseLayer(true).ok, "orphan symbol treated as a layer");
rows = [cad, survey];
survey.addLock = true;
assert.ok(!chooseLayer(true).ok && layers.activeLayer === survey, "add silently switched to CAD");
assert.ok(chooseLayer(false).ok);
layers.activeLayer = null; assert.ok(!chooseLayer(false).ok);
rows = [survey]; assert.ok(chooseLayer(false).ok && layers.activeLayer === survey);
layers.projectInfo.editRights = false; assert.ok(!chooseLayer(false).ok);
survey.addLock = false; assert.ok(chooseLayer(true).ok);
console.log("PASS expanded and orphan legend rows, layer ambiguity, read-only, locked add or edit, project rights");

const board = {activeLayer: cad, canEditLayerAttributes: () => true};
const role = {LayerRole: "layer", FeatureIdRole: "fid"};
let hits = [], filter, callbacks = [], captures = 0;
const form = {selection: {focusedItem: -1}, model: {
  get count() { return hits.length; }, index: x => x, data: (i, r) => hits[i][r],
  setFeatures: (layer, expr) => { filter = [layer, expr]; }
}};
const edit = context({mainWindow: {sungsanStartSurvey: () => true, sungsanPrepareFieldPhotoLayer() {}},
  dashBoard: board, featureListForm: form, MultiFeatureListModel: role, isPieMenuRequest: false, isMenuRequest: false,
  qfieldSettings: {}, positionSource: {active: false}, coordinateLocator: {}, stateMachine: {state: "digitize"},
  Qgis: layers.Qgis, digitizingToolbar: {digitizingAllowed: true, triggerAddVertex() {++captures;}},
  Qt: {callLater: (f) => callbacks.push(f)}});
const identify = handler(edit, "onIdentifyFinished:");
const add = handler(edit, "onAddFeatureRequested:");
function editHits(layer, matched) {
  board.activeLayer = layer; hits = matched; filter = null; form.state = "";
  edit.mainWindow.sungsanExistingFeatureEditPending = true; identify();
}
editHits(survey, [{layer:cad, fid:8}, {layer:survey, fid:8}, {layer:survey, fid:9}]);
assert.deepEqual(filter,[survey, "@id IN (8,9)"]); assert.equal(form.state,"FeatureList");
editHits(survey,[{layer:cad, fid:8}]); assert.deepEqual(filter,[cad,"@id = 8"]); assert.equal(form.state,"FeatureFormEdit");
editHits(survey,[]); assert.equal(filter,null); assert.equal(form.state,"");
add(); assert.equal(edit.mainWindow.sungsanExistingFeatureEditPending, false);
board.activeLayer = cad; callbacks.shift()(); assert.equal(captures, 0, "queued capture changed user-selected layer");
add(); callbacks.shift()(); assert.equal(captures, 1);
edit.positionSource.active=true; edit.coordinateLocator.positionLocked=true; edit.mainWindow.sungsanGnssFresh=false;
add(); assert.equal(callbacks.length, 0); assert.equal(captures, 1);
console.log("PASS overlapping feature IDs across layers, multiple hits, empty layer, cancel selection, deferred point and stale GNSS");

const toolbarQml = fs.readFileSync(path.join(__dirname, "../src/qml/DigitizingToolbar.qml"), "utf8");
let hardwareAdded = 0, handledByGeometryTool = 0;
const hardware = context({
  mainWindow: {sungsanGnssFresh: false, sungsanPositionSwitchPending: false},
  coordinateLocator: {positionLocked: true}, positionSource: {active: true}, positioningSettings: {},
  Qt: {Key_VolumeUp: 1, Key_VolumeDown: 2}, stateMachine: {state: "digitize"}, mapCanvasMap: {isEnabled: true},
  digitizingToolbar: {digitizingAllowed: true, triggerAddVertex() { ++hardwareAdded; }},
  geometryEditorsToolbar: {canvasClicked() { ++handledByGeometryTool; return false; }}
});
hardware.digitizingToolbar.checkAccuracyRequirement = vm.runInContext("(function()" + body("function checkAccuracyRequirement", 0, toolbarQml) + ")", hardware);
const volumeUp = handler(hardware, "function onVolumeKeyUp", "volumeKeyCode");
volumeUp(1); assert.equal(hardwareAdded, 0); assert.equal(handledByGeometryTool, 0);
hardware.mainWindow.sungsanGnssFresh = true; volumeUp(1); assert.equal(hardwareAdded, 1);
hardware.mainWindow.sungsanPositionSwitchPending = true; volumeUp(1); assert.equal(hardwareAdded, 1);
hardware.mainWindow.sungsanPositionSwitchPending = false; hardware.positionSource.active = false; volumeUp(1); assert.equal(hardwareAdded, 1);
hardware.coordinateLocator.positionLocked = false; volumeUp(1); assert.equal(hardwareAdded, 2);
hardware.digitizingToolbar.digitizingAllowed = false; volumeUp(1); assert.equal(hardwareAdded, 2);
console.log("PASS hardware volume-key point capture: no stale, switching, inactive or locked-layer coordinates; fresh and manual capture pass");
