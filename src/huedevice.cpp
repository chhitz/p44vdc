//
//  huedevice.cpp
//  vdcd
//
//  Created by Lukas Zeller on 02.09.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#include "huedevice.hpp"
#include "huedevicecontainer.hpp"

#include "fnv.hpp"

#include "lightbehaviour.hpp"

using namespace p44;


#pragma mark - HueLightScene


HueLightScene::HueLightScene(SceneDeviceSettings &aSceneDeviceSettings, SceneNo aSceneNo) :
  inherited(aSceneDeviceSettings, aSceneNo)
{
  colorMode = hueColorModeNone;
  XOrHueOrCt = 0;
  YOrSat = 0;
}


#pragma mark - HueLight Scene persistence

const char *HueLightScene::tableName()
{
  return "HueLightScenes";
}

// data field definitions

static const size_t numHueSceneFields = 3;

size_t HueLightScene::numFieldDefs()
{
  return inherited::numFieldDefs()+numHueSceneFields;
}


const FieldDefinition *HueLightScene::getFieldDef(size_t aIndex)
{
  static const FieldDefinition dataDefs[numHueSceneFields] = {
    { "colorMode", SQLITE_INTEGER },
    { "XOrHueOrCt", SQLITE_FLOAT },
    { "YOrSat", SQLITE_FLOAT }
  };
  if (aIndex<inherited::numFieldDefs())
    return inherited::getFieldDef(aIndex);
  aIndex -= inherited::numFieldDefs();
  if (aIndex<numHueSceneFields)
    return &dataDefs[aIndex];
  return NULL;
}



/// load values from passed row
void HueLightScene::loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex)
{
  inherited::loadFromRow(aRow, aIndex);
  // get the fields
  colorMode = (HueColorMode)aRow->get<int>(aIndex++);
  XOrHueOrCt = aRow->get<double>(aIndex++);
  YOrSat = aRow->get<double>(aIndex++);
}


/// bind values to passed statement
void HueLightScene::bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier)
{
  inherited::bindToStatement(aStatement, aIndex, aParentIdentifier);
  // bind the fields
  aStatement.bind(aIndex++, (int)colorMode);
  aStatement.bind(aIndex++, XOrHueOrCt);
  aStatement.bind(aIndex++, YOrSat);
}


#pragma mark - Light scene property access


static char huelightscene_key;

enum {
  colorMode_key,
  hue_key,
  saturation_key,
  X_key,
  Y_key,
  colorTemperature_key,
  numHueLightSceneProperties
};


int HueLightScene::numProps(int aDomain)
{
  return inherited::numProps(aDomain)+numHueLightSceneProperties;
}


const PropertyDescriptor *HueLightScene::getPropertyDescriptor(int aPropIndex, int aDomain)
{
  static const PropertyDescriptor properties[numHueLightSceneProperties] = {
    { "colorMode", ptype_int8, false, colorMode_key, &huelightscene_key },
    { "hue", ptype_double, false, hue_key, &huelightscene_key },
    { "saturation", ptype_double, false, saturation_key, &huelightscene_key },
    { "X", ptype_double, false, X_key, &huelightscene_key },
    { "Y", ptype_double, false, Y_key, &huelightscene_key },
    { "colorTemperature", ptype_double, false, colorTemperature_key, &huelightscene_key },
  };
  int n = inherited::numProps(aDomain);
  if (aPropIndex<n)
    return inherited::getPropertyDescriptor(aPropIndex, aDomain); // base class' property
  aPropIndex -= n; // rebase to 0 for my own first property
  return &properties[aPropIndex];
}


bool HueLightScene::accessField(bool aForWrite, JsonObjectPtr &aPropValue, const PropertyDescriptor &aPropertyDescriptor, int aIndex)
{
  if (aPropertyDescriptor.objectKey==&huelightscene_key) {
    if (!aForWrite) {
      // read properties
      switch (aPropertyDescriptor.accessKey) {
        case colorMode_key:
          aPropValue = JsonObject::newInt32(colorMode);
          return true;
        case hue_key:
          aPropValue = colorMode==hueColorModeHueSaturation ? JsonObject::newDouble(XOrHueOrCt) : JsonObjectPtr();
          return true;
        case saturation_key:
          aPropValue = colorMode==hueColorModeHueSaturation ? JsonObject::newDouble(YOrSat) : JsonObjectPtr();
          return true;
        case X_key:
          aPropValue = colorMode==hueColorModeXY ? JsonObject::newDouble(XOrHueOrCt) : JsonObjectPtr();
          return true;
        case Y_key:
          aPropValue = colorMode==hueColorModeXY ? JsonObject::newDouble(YOrSat) : JsonObjectPtr();
          return true;
        case colorTemperature_key:
          aPropValue = colorMode==hueColorModeCt ? JsonObject::newDouble(XOrHueOrCt) : JsonObjectPtr();
          return true;
      }
    }
    else {
      // write properties
      switch (aPropertyDescriptor.accessKey) {
        case colorMode_key:
          colorMode = (HueColorMode)aPropValue->int32Value();
          markDirty();
          return true;
        case hue_key:
          colorMode = hueColorModeHueSaturation;
          goto setXOrHueOrCt;
        case X_key:
          colorMode = hueColorModeXY;
          goto setXOrHueOrCt;
        case colorTemperature_key:
          colorMode = hueColorModeCt;
        setXOrHueOrCt:
          XOrHueOrCt = aPropValue->doubleValue();
          markDirty();
          return true;
        case saturation_key:
          colorMode = hueColorModeHueSaturation;
          goto setYOrSat;
        case Y_key:
          colorMode = hueColorModeXY;
        setYOrSat:
          YOrSat = aPropValue->doubleValue();
          markDirty();
          return true;
      }
    }
  }
  return inherited::accessField(aForWrite, aPropValue, aPropertyDescriptor, aIndex);
}


#pragma mark - default scene values


void HueLightScene::setDefaultSceneValues(SceneNo aSceneNo)
{
  // init default brightness
  inherited::setDefaultSceneValues(aSceneNo);
  // init hue specifics
  // TODO: maybe, more elaborated defaults
  colorMode = hueColorModeNone; // no stored color information
}



#pragma mark - HueDeviceSettings with default light scenes factory


HueDeviceSettings::HueDeviceSettings(Device &aDevice) :
  inherited(aDevice)
{
};


DsScenePtr HueDeviceSettings::newDefaultScene(SceneNo aSceneNo)
{
  HueLightScenePtr lightScene = HueLightScenePtr(new HueLightScene(*this, aSceneNo));
  lightScene->setDefaultSceneValues(aSceneNo);
  // return it
  return lightScene;
}


#pragma mark - HueLightBehaviour


HueLightBehaviour::HueLightBehaviour(Device &aDevice) :
  LightBehaviour(aDevice)
{
}


void HueLightBehaviour::recallScene(LightScenePtr aLightScene)
{
  HueLightScenePtr hueScene = boost::dynamic_pointer_cast<HueLightScene>(aLightScene);
  if (hueScene) {
    // prepare next color values in device
    HueDevice *devP = dynamic_cast<HueDevice *>(&device);
    if (devP) {
      devP->pendingColorScene = hueScene;
    }
  }
  // let base class update logical brightness, which will in turn update the output, which will then
  // catch the colors from pendingColorScene
  inherited::recallScene(aLightScene);
}



// capture scene
void HueLightBehaviour::captureScene(DsScenePtr aScene)
{
  HueLightScenePtr hueScene = boost::dynamic_pointer_cast<HueLightScene>(aScene);
  if (hueScene) {
    // query light attributes and state
    HueDevice *devP = dynamic_cast<HueDevice *>(&device);
    if (devP) {
      string url = string_format("/lights/%s", devP->lightID.c_str());
      devP->hueComm().apiQuery(url.c_str(), boost::bind(&HueLightBehaviour::sceneColorsReceived, this, hueScene, _2, _3));
    }
  }
}


void HueLightBehaviour::sceneColorsReceived(HueLightScenePtr aHueScene, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    JsonObjectPtr o;
    // get current color settings
    JsonObjectPtr state = aDeviceInfo->get("state");
    HueColorMode newMode = hueColorModeNone;
    double newXOrHueOrCt = 0;
    double newYOrSat = 0;
    if (state) {
      o = state->get("colormode");
      if (o) {
        string mode = o->stringValue();
        if (mode=="hs") {
          newMode = hueColorModeHueSaturation;
          o = state->get("hue");
          if (o) newXOrHueOrCt = o->int32Value();
          o = state->get("sat");
          if (o) newYOrSat = o->int32Value();
        }
        else if (mode=="xy") {
          newMode = hueColorModeXY;
          o = state->get("xy");
          if (o) {
            JsonObjectPtr e = o->arrayGet(0);
            if (e) newXOrHueOrCt = e->doubleValue();
            e = o->arrayGet(1);
            if (e) newYOrSat = e->doubleValue();
          }
        }
        else if (mode=="ct") {
          newMode = hueColorModeCt;
          o = state->get("ct");
          if (o) newXOrHueOrCt = o->int32Value();
        }
      }
      // check color updates
      if (newMode!=aHueScene->colorMode) { aHueScene->colorMode = newMode; aHueScene->markDirty(); }
      if (newXOrHueOrCt!=aHueScene->XOrHueOrCt) { aHueScene->XOrHueOrCt = newXOrHueOrCt; aHueScene->markDirty(); }
      if (newXOrHueOrCt!=aHueScene->YOrSat) { aHueScene->YOrSat = newYOrSat; aHueScene->markDirty(); }
      // in any case, update current output value (cache in outputbehaviour) as well, so base class will capture a current level
      o = state->get("bri");
      if (o) {
        Brightness bri = (Brightness)o->int32Value();
        initOutputValue(bri);
      }
    }
  }
  // anyway, let base class capture brightness
  inherited::captureScene(aHueScene);
  // as we deferred capture, we need to update the scene again to have it persisted
  if (aHueScene->isDirty()) {
    device.updateScene(aHueScene);
  }
}


#pragma mark - HueDevice


HueDevice::HueDevice(HueDeviceContainer *aClassContainerP, const string &aLightID) :
  inherited(aClassContainerP),
  lightID(aLightID)
{
  // hue devices are lights
  setPrimaryGroup(group_yellow_light);
  // derive the dSID
  deriveDSID();
  // use light settings, which include a scene table
  deviceSettings = DeviceSettingsPtr(new HueDeviceSettings(*this));
  // set the behaviour
  HueLightBehaviourPtr l = HueLightBehaviourPtr(new HueLightBehaviour(*this));
  l->setHardwareOutputConfig(outputFunction_dimmer, true, 8.5); // hue lights are always dimmable, one hue = 8.5W
  l->setHardwareName(string_format("brightness of hue light #%s", lightID.c_str()));
  l->initBrightnessParams(1,255); // brightness range is 1..255
  addBehaviour(l);
}



HueDeviceContainer &HueDevice::hueDeviceContainer()
{
  return *(static_cast<HueDeviceContainer *>(classContainerP));
}


HueComm &HueDevice::hueComm()
{
  return (static_cast<HueDeviceContainer *>(classContainerP))->hueComm;
}



void HueDevice::setName(const string &aName)
{
  string oldname = getName();
  inherited::setName(aName);
  if (getName()!=oldname) {
    // really changed, propagate to hue
    JsonObjectPtr params = JsonObject::newObj();
    params->add("name", JsonObject::newString(getName()));
    string url = string_format("/lights/%s", lightID.c_str());
    hueComm().apiAction(httpMethodPUT, url.c_str(), params, NULL);
  }
}




void HueDevice::initializeDevice(CompletedCB aCompletedCB, bool aFactoryReset)
{
  // query light attributes and state
  string url = string_format("/lights/%s", lightID.c_str());
  hueComm().apiQuery(url.c_str(), boost::bind(&HueDevice::deviceStateReceived, this, aCompletedCB, aFactoryReset, _2, _3));
}


void HueDevice::deviceStateReceived(CompletedCB aCompletedCB, bool aFactoryReset, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  if (Error::isOK(aError) && aDeviceInfo) {
    JsonObjectPtr o;
    // get model name from device
    hueModel.clear();
    o = aDeviceInfo->get("type");
    if (o) {
      hueModel = o->stringValue();
    }
    o = aDeviceInfo->get("modelid");
    if (o) {
      hueModel += ": " + o->stringValue();
    }
    // get current brightness
    JsonObjectPtr state = aDeviceInfo->get("state");
    Brightness bri = 0;
    if (state) {
      o = state->get("on");
      if (o && o->boolValue()) {
        // lamp is on
        bri = 255; // default to full brightness
        o = state->get("bri");
        if (o) {
          bri = o->int32Value();
        }
        // set current brightness
        boost::static_pointer_cast<LightBehaviour>(outputs[0])->initOutputValue(bri);
      }
    }
  }
  // let superclass initialize as well
  inherited::initializeDevice(aCompletedCB, aFactoryReset);
}



void HueDevice::checkPresence(PresenceCB aPresenceResultHandler)
{
  // query the device
  string url = string_format("/lights/%s", lightID.c_str());
  hueComm().apiQuery(url.c_str(), boost::bind(&HueDevice::presenceStateReceived, this, aPresenceResultHandler, _2, _3));
}



void HueDevice::presenceStateReceived(PresenceCB aPresenceResultHandler, JsonObjectPtr aDeviceInfo, ErrorPtr aError)
{
  bool reachable = false;
  if (Error::isOK(aError) && aDeviceInfo) {
    JsonObjectPtr state = aDeviceInfo->get("state");
    if (state) {
      // Note: 2012 hue bridge firmware always returns 1 for this.
      JsonObjectPtr o = state->get("reachable");
      reachable = o && o->boolValue();
    }
  }
  aPresenceResultHandler(reachable);
}



void HueDevice::identifyToUser()
{
  // Four breathe cycles
  alertHandler(4);
//  // standard LightBehaviour blink
//  LightBehaviourPtr l = boost::dynamic_pointer_cast<LightBehaviour>(outputs[0]);
//  if (l) {
//    l->blink(4*Second);
//  }
}


void HueDevice::alertHandler(int aLeftCycles)
{
  // do one alert
  string url = string_format("/lights/%s/state", lightID.c_str());
  JsonObjectPtr newState = JsonObject::newObj();
  newState->add("alert", JsonObject::newString("select"));
  hueComm().apiAction(httpMethodPUT, url.c_str(), newState, NULL);
  // schedule next if any left
  if (--aLeftCycles>0) {
    MainLoop::currentMainLoop().executeOnce(boost::bind(&HueDevice::alertHandler, this, aLeftCycles), 1*Second);
  }
}



void HueDevice::disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler)
{
  checkPresence(boost::bind(&HueDevice::disconnectableHandler, this, aForgetParams, aDisconnectResultHandler, _1));
}


void HueDevice::disconnectableHandler(bool aForgetParams, DisconnectCB aDisconnectResultHandler, bool aPresent)
{
  if (!aPresent) {
    // call inherited disconnect
    inherited::disconnect(aForgetParams, aDisconnectResultHandler);
  }
  else {
    // not disconnectable
    if (aDisconnectResultHandler) {
      aDisconnectResultHandler(classContainerP->getDevicePtrForInstance(this), false);
    }
  }
}




void HueDevice::updateOutputValue(OutputBehaviour &aOutputBehaviour)
{
  if (aOutputBehaviour.getIndex()==0) {
    string url = string_format("/lights/%s/state", lightID.c_str());
    JsonObjectPtr newState = JsonObject::newObj();
    Brightness b = aOutputBehaviour.valueForHardware();
    if (b==0) {
      // light off
      newState->add("on", JsonObject::newBool(false));
    }
    else {
      // light on
      newState->add("on", JsonObject::newBool(true));
      newState->add("bri", JsonObject::newInt32(b)); // 0..255
      // add color in case it was set (by scene call)
      if (pendingColorScene) {
        const char *cm;
        switch (pendingColorScene->colorMode) {
          case hueColorModeHueSaturation: {
            newState->add("hue", JsonObject::newInt32(pendingColorScene->XOrHueOrCt));
            newState->add("sat", JsonObject::newInt32(pendingColorScene->YOrSat));
            cm = "hs";
            break;
          }
          case hueColorModeXY: {
            JsonObjectPtr xyArr = JsonObject::newArray();
            xyArr->arrayAppend(JsonObject::newDouble(pendingColorScene->XOrHueOrCt));
            xyArr->arrayAppend(JsonObject::newDouble(pendingColorScene->YOrSat));
            newState->add("xy", xyArr);
            cm = "xy";
            break;
          }
          case hueColorModeCt: {
            newState->add("ct", JsonObject::newInt32(pendingColorScene->XOrHueOrCt));
            cm = "ct"; break;
          }
          default:
            cm = NULL;
        }
        if (cm) {
          newState->add("colormode", JsonObject::newString(cm));
        }
        // done
        pendingColorScene.reset();
      }
    }
    // for on and off, set transition time (1/10 second resolution)
    newState->add("transitiontime", JsonObject::newInt64(aOutputBehaviour.transitionTimeForHardware()/(100*MilliSecond)));
    LOG(LOG_DEBUG, "HueDevice: setting new state, brightness = %d, JSON = %s\n", b, newState->c_strValue());
    hueComm().apiAction(httpMethodPUT, url.c_str(), newState, NULL);
  }
  else
    return inherited::updateOutputValue(aOutputBehaviour); // let superclass handle this
}



void HueDevice::deriveDSID()
{
  Fnv64 hash;
	
	// we have no unqiquely defining device information, construct something as reproducible as possible
	// - use class container's ID
	string s = classContainerP->deviceClassContainerInstanceIdentifier();
	hash.addBytes(s.size(), (uint8_t *)s.c_str());
	// - add-in the console device name
  #warning "TEST ONLY: lightID is only semi-clever basis for a hash!!!"
  hash.addBytes(lightID.length(), (uint8_t *)lightID.c_str());
  #if FAKE_REAL_DSD_IDS
  dsid.setObjectClass(DSID_OBJECTCLASS_DSDEVICE);
  dsid.setSerialNo(hash.getHash28()<<4); // leave lower 4 bits for input number
  #warning "TEST ONLY: faking digitalSTROM device addresses, possibly colliding with real devices"
  #else
  dsid.setObjectClass(DSID_OBJECTCLASS_MACADDRESS); // TODO: validate, now we are using the MAC-address class with bits 48..51 set to 7
  dsid.setSerialNo(0x7000000000000ll+hash.getHash48());
  #endif
  // TODO: validate, now we are using the MAC-address class with bits 48..51 set to 7
}


string HueDevice::description()
{
  string s = inherited::description();
  return s;
}
