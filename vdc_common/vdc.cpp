//
//  Copyright (c) 2013-2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44vdc.
//
//  p44vdc is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44vdc is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44vdc. If not, see <http://www.gnu.org/licenses/>.
//

#include "vdc.hpp"

#include "device.hpp"

using namespace p44;


Vdc::Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag) :
  inherited(aVdcHostP),
  inheritedParams(aVdcHostP->getDsParamStore()),
  instanceNumber(aInstanceNumber),
  defaultZoneID(0),
  vdcFlags(0),
  tag(aTag),
  pairTicket(0)
{
}



string Vdc::modelUID()
{
  // use vDC identifier as modelID
  DsUid vdcNamespace(DSUID_P44VDC_MODELUID_UUID);
  // now make UUIDv5 type dSUID out of it
  DsUid modelUID;
  modelUID.setNameInSpace(vdcClassIdentifier(), vdcNamespace);
  return modelUID.getString();
}


string Vdc::getName()
{
  if (inherited::getName().empty()) {
    // no name set for this vdc
    // - check if vdc host has a name
    if (!getVdcHost().getName().empty()) {
      // there is a custom name set for the entire vdc host, use it as base for default names
      return string_format("%s %s", getVdcHost().getName().c_str(), vdcModelSuffix().c_str());
    }
  }
  // just use assigned name
  return inherited::getName();
}


void Vdc::setName(const string &aName)
{
  if (aName!=getAssignedName()) {
    // has changed
    inherited::setName(aName);
    // make sure it will be saved
    markDirty();
  }
}


ErrorPtr Vdc::handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams)
{
  ErrorPtr respErr;
  if (aMethod=="scanDevices") {
    // vDC API v2c addition, only via genericRequest
    // (re)collect devices of this particular vDC
    bool incremental = true;
    bool exhaustive = false;
    bool clear = false;
    checkBoolParam(aParams, "incremental", incremental);
    checkBoolParam(aParams, "exhaustive", exhaustive);
    checkBoolParam(aParams, "clearconfig", clear);
    collectDevices(boost::bind(&DsAddressable::methodCompleted, this, aRequest, _1), incremental, exhaustive, clear);
  }
  else if (aMethod=="pair") {
    // only via genericRequest
    // (re)collect devices of this particular vDC
    Tristate establish = undefined; // default to pair or unpair
    ApiValuePtr o = aParams->get("establish");
    if (o && !o->isNull()) {
      establish = o->boolValue() ? yes : no;
    }
    bool disableProximityCheck = false; // default to proximity check enabled (if technology can detect proximity)
    checkBoolParam(aParams, "disableProximityCheck", disableProximityCheck);
    int timeout = 30; // default to 30 seconds timeout
    o = aParams->get("timeout");
    if (o) {
      timeout = o->int32Value();
    }
    // actually run the pairing process
    performPair(aRequest, establish, disableProximityCheck, timeout*Second);
  }
  else {
    respErr = inherited::handleMethod(aRequest, aMethod, aParams);
  }
  return respErr;
}


void Vdc::performPair(VdcApiRequestPtr aRequest, Tristate aEstablish, bool aDisableProximityCheck, MLMicroSeconds aTimeout)
{
  // anyway - first stop any device-wide learn that might still be running on this or other vdcs
  MainLoop::currentMainLoop().cancelExecutionTicket(pairTicket);
  getVdcHost().stopLearning();
  if (aTimeout<=0) {
    // calling with timeout==0 means aborting learn (which has already happened by now)
    // - confirm with OK
    ALOG(LOG_NOTICE, "- pairing aborted");
    aRequest->sendStatus(Error::err<VdcApiError>(404, "pairing/unpairing aborted"));
    return;
  }
  // start new pairing
  ALOG(LOG_NOTICE, "Starting single vDC pairing");
  pairTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&Vdc::pairingTimeout, this, aRequest), aTimeout);
  getVdcHost().learnHandler = boost::bind(&Vdc::pairingEvent, this, aRequest, _1, _2);
  setLearnMode(true, aDisableProximityCheck, aEstablish);
}


void Vdc::pairingEvent(VdcApiRequestPtr aRequest, bool aLearnIn, ErrorPtr aError)
{
  MainLoop::currentMainLoop().cancelExecutionTicket(pairTicket);
  if (Error::isOK(aError)) {
    if (aLearnIn) {
      // learned in something
      ALOG(LOG_NOTICE, "- pairing established");
      aRequest->sendStatus(Error::ok());
    }
    else {
      // learned out something
      ALOG(LOG_NOTICE, "- pairing removed");
      aRequest->sendStatus(Error::err<VdcApiError>(410, "device unpaired"));
    }
  }
  else {
    aRequest->sendError(aError);
  }
}


void Vdc::pairingTimeout(VdcApiRequestPtr aRequest)
{
  getVdcHost().stopLearning();
  ALOG(LOG_NOTICE, "- timeout: no pairing or unpairing occurred");
  aRequest->sendStatus(Error::err<VdcApiError>(404, "timeout, no (un)pairing event occurred"));
}


void Vdc::addVdcToVdcHost()
{
  // derive dSUID first, as it will be mapped by dSUID in the device container 
  deriveDsUid();
  // add to container
  getVdcHost().addVdc(VdcPtr(this));
}



void Vdc::initialize(StatusCB aCompletedCB, bool aFactoryReset)
{
  // done
	aCompletedCB(ErrorPtr()); // default to error-free initialisation
}


void Vdc::selfTest(StatusCB aCompletedCB)
{
  // by default, assume everything ok
  aCompletedCB(ErrorPtr());
}


const char *Vdc::getPersistentDataDir()
{
	return getVdcHost().getPersistentDataDir();
}


int Vdc::getInstanceNumber() const
{
	return instanceNumber;
}


void Vdc::deriveDsUid()
{
  // class containers have v5 UUIDs based on the device container's master UUID as namespace
  string name = string_format("%s.%d", vdcClassIdentifier(), getInstanceNumber()); // name is class identifier plus instance number: classID.instNo
  dSUID.setNameInSpace(name, getVdcHost().dSUID); // domain is dSUID of device container
}


string Vdc::vdcInstanceIdentifier() const
{
  string s(vdcClassIdentifier());
  string_format_append(s, ".%d@", getInstanceNumber());
  s.append(getVdcHost().dSUID.getString());
  return s;
}


bool Vdc::getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix)
{
  if (getIcon("vdc", aIcon, aWithData, aResolutionPrefix))
    return true;
  else
    return inherited::getDeviceIcon(aIcon, aWithData, aResolutionPrefix);
}


string Vdc::vendorName()
{
  // default to same vendor as vdc host (device container)
  return getVdcHost().vendorName();
}



// add a device
bool Vdc::addDevice(DevicePtr aDevice)
{
  // let device consider its internal structure and dSUID for the last time
  aDevice->willBeAdded();
  // announce to global device container
  if (getVdcHost().addDevice(aDevice)) {
    // not a duplicate
    // - save in my own list
    devices.push_back(aDevice);
    // added
    return true;
  }
  return false;
}



// remove a device
void Vdc::removeDevice(DevicePtr aDevice, bool aForget)
{
	// find and remove from my list.
	for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
		if (*pos==aDevice) {
			devices.erase(pos);
			break;
		}
	}
  // remove from global device container
  getVdcHost().removeDevice(aDevice, aForget);
}


void Vdc::removeDevices(bool aForget)
{
	for (DeviceVector::iterator pos = devices.begin(); pos!=devices.end(); ++pos) {
    DevicePtr dev = *pos;
    // inform upstream about these devices going offline now (if API connection is up at all at this time)
    dev->reportVanished();
    // now actually remove
    getVdcHost().removeDevice(dev, aForget);
  }
  // clear my own list
  devices.clear();
}



// MARK: ===== persistent vdc level params


ErrorPtr Vdc::load()
{
  ErrorPtr err;
  // load the vdc settings
  err = loadFromStore(dSUID.getString().c_str());
  if (!Error::isOK(err)) ALOG(LOG_ERR,"Error loading settings: %s", err->description().c_str());
  loadSettingsFromFiles();
  return ErrorPtr();
}


ErrorPtr Vdc::save()
{
  ErrorPtr err;
  // save the vdc settings
  err = saveToStore(dSUID.getString().c_str(), false); // only one record per vdc
  return ErrorPtr();
}


ErrorPtr Vdc::forget()
{
  // delete the vdc settings
  deleteFromStore();
  return ErrorPtr();
}


void Vdc::loadSettingsFromFiles()
{
  string dir = getVdcHost().getPersistentDataDir();
  const int numLevels = 2;
  string levelids[numLevels];
  // Level strategy: most specialized will be active, unless lower levels specify explicit override
  // - Baselines are hardcoded defaults plus settings (already) loaded from persistent store
  // - Level 0 are settings related to the device instance (dSUID)
  // - Level 1 are settings related to the vDC (vdcClassIdentifier())
  levelids[0] = getDsUid().getString();
  levelids[1] = vdcClassIdentifier();
  for(int i=0; i<numLevels; ++i) {
    // try to open config file
    string fn = dir+"vdcsettings_"+levelids[i]+".csv";
    // if vdc has already stored properties, only explicitly marked properties will be applied
    if (loadSettingsFromFile(fn.c_str(), rowid!=0)) markClean();
  }
}


// MARK: ===== property access

static char deviceclass_key;
static char device_container_key;
static char capabilities_container_key;
static char device_key;

enum {
  webui_url_key,
  defaultzone_key,
  capabilities_key,
  devices_key,
  deviceclassidentifier_key,
  instancenumber_key,
  rescanModes_key,
  numClassContainerProperties
};


enum {
  capability_metering_key,
  numCapabilities
};



int Vdc::numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(device_container_key)) {
    return (int)devices.size();
  }
  else if (aParentDescriptor->hasObjectKey(capabilities_container_key)) {
    return numCapabilities;
  }
  return inherited::numProps(aDomain, aParentDescriptor)+numClassContainerProperties;
}


PropertyDescriptorPtr Vdc::getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(device_container_key)) {
    // accessing one of the devices by numeric index
    return getDescriptorByNumericName(
      aPropMatch, aStartIndex, aDomain, aParentDescriptor,
      OKEY(device_key)
    );
  }
  // None of the containers within Device - let base class handle vdc-Level properties
  return inherited::getDescriptorByName(aPropMatch, aStartIndex, aDomain, aMode, aParentDescriptor);
}


PropertyContainerPtr Vdc::getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain)
{
  if (aPropertyDescriptor->isArrayContainer()) {
    // local container
    return PropertyContainerPtr(this); // handle myself
  }
  else if (aPropertyDescriptor->hasObjectKey(device_key)) {
    // - get device
    PropertyContainerPtr container = devices[aPropertyDescriptor->fieldKey()];
    return container;
  }
  // unknown here
  return NULL;
}



// note: is only called when getDescriptorByName does not resolve the name
PropertyDescriptorPtr Vdc::getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor)
{
  if (aParentDescriptor->hasObjectKey(capabilities_container_key)) {
    // capabilities level
    static const PropertyDescription capability_props[numClassContainerProperties] = {
      { "metering", apivalue_bool, capability_metering_key, OKEY(capabilities_container_key) },
    };
    // simple, all on this level
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&capability_props[aPropIndex], aParentDescriptor));
  }
  else {
    // vdc level
    static const PropertyDescription properties[numClassContainerProperties] = {
      { "configURL", apivalue_string, webui_url_key, OKEY(deviceclass_key) },
      { "zoneID", apivalue_uint64, defaultzone_key, OKEY(deviceclass_key) },
      { "capabilities", apivalue_object+propflag_container, capabilities_key, OKEY(capabilities_container_key) },
      { "x-p44-devices", apivalue_object+propflag_container+propflag_nowildcard, devices_key, OKEY(device_container_key) },
      { "x-p44-deviceClass", apivalue_string, deviceclassidentifier_key, OKEY(deviceclass_key) },
      { "x-p44-instanceNo", apivalue_uint64, instancenumber_key, OKEY(deviceclass_key) },
      { "x-p44-rescanModes", apivalue_uint64, rescanModes_key, OKEY(deviceclass_key) }
    };
    int n = inherited::numProps(aDomain, aParentDescriptor);
    if (aPropIndex<n)
      return inherited::getDescriptorByIndex(aPropIndex, aDomain, aParentDescriptor); // base class' property
    aPropIndex -= n; // rebase to 0 for my own first property
    return PropertyDescriptorPtr(new StaticPropertyDescriptor(&properties[aPropIndex], aParentDescriptor));
  }
}



bool Vdc::accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor)
{
  if (aPropertyDescriptor->hasObjectKey(deviceclass_key)) {
    // vdc level properties
    if (aMode==access_read) {
      // read
      switch (aPropertyDescriptor->fieldKey()) {
        case webui_url_key:
          aPropValue->setStringValue(webuiURLString());
          return true;
        case defaultzone_key:
          aPropValue->setUint16Value(defaultZoneID);
          return true;
        case deviceclassidentifier_key:
          aPropValue->setStringValue(vdcClassIdentifier());
          return true;
        case instancenumber_key:
          aPropValue->setUint32Value(getInstanceNumber());
          return true;
        case rescanModes_key:
          aPropValue->setUint32Value(getRescanModes());
          return true;
      }
    }
    else {
      // write
      switch (aPropertyDescriptor->fieldKey()) {
        case defaultzone_key:
          setPVar(defaultZoneID, aPropValue->int32Value());
          return true;
      }
    }
  }
  else if (aPropertyDescriptor->hasObjectKey(capabilities_container_key)) {
    // capabilities
    if (aMode==access_read) {
      switch (aPropertyDescriptor->fieldKey()) {
        case capability_metering_key: aPropValue->setBoolValue(false); return true; // TODO: implement actual metering flag
      }
    }
  }
  // not my field, let base class handle it
  return inherited::accessField(aMode, aPropValue, aPropertyDescriptor);
}


// MARK: ===== persistence implementation

// SQLIte3 table name to store these parameters to
const char *Vdc::tableName()
{
  return "VdcSettings";
}


// data field definitions

static const size_t numFields = 3;

size_t Vdc::numFieldDefs()
{
  return inheritedParams::numFieldDefs()+numFields;
}


const FieldDefinition *Vdc::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numFields] = {
    { "vdcFlags", SQLITE_INTEGER },
    { "vdcName", SQLITE_TEXT },
    { "defaultZoneID", SQLITE_INTEGER }
  };
  if (aIndex<inheritedParams::numFieldDefs())
    return inheritedParams::getFieldDef(aIndex);
  aIndex -= inheritedParams::numFieldDefs();
  if (aIndex<numFields)
    return &dataDefs[aIndex];
  return NULL;
}


/// load values from passed row
void Vdc::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP)
{
  inheritedParams::loadFromRow(aRow, aIndex, aCommonFlagsP);
  // get the field value
  vdcFlags = aRow->get<int>(aIndex++);
  setName(nonNullCStr(aRow->get<const char *>(aIndex++)));
  defaultZoneID = aRow->get<int>(aIndex++);
}


// bind values to passed statement
void Vdc::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags)
{
  inheritedParams::bindToStatement(aStatement, aIndex, aParentIdentifier, aCommonFlags);
  // bind the fields
  aStatement.bind(aIndex++, vdcFlags);
  aStatement.bind(aIndex++, getAssignedName().c_str(), false); // c_str() ist not static in general -> do not rely on it (even if static here)
  aStatement.bind(aIndex++, defaultZoneID);
}

// MARK: ===== description/shortDesc


string Vdc::description()
{
  string d = string_format("%s #%d: %s (%ld devices)", vdcClassIdentifier(), getInstanceNumber(), shortDesc().c_str(), (long)devices.size());
  return d;
}



