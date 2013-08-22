//
//  gpiodevice.cpp
//  vdcd
//
//  Created by Lukas Zeller on 18.04.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#include "gpiodevice.hpp"

#include "fnv.hpp"

#include "buttonbehaviour.hpp"
#include "lightbehaviour.hpp"

using namespace p44;


GpioDevice::GpioDevice(StaticDeviceContainer *aClassContainerP, const string &aDeviceConfig) :
  Device((DeviceClassContainer *)aClassContainerP)
{
  size_t i = aDeviceConfig.find_first_of(':');
  string gpioname = aDeviceConfig;
  isOutput = false;
  inverted = false;
  if (i!=string::npos) {
    gpioname = aDeviceConfig.substr(0,i);
    string mode = aDeviceConfig.substr(i+1,string::npos);
    if (mode[0]=='!') {
      inverted = true;
      mode.erase(0,1);
    }
    if (mode=="in")
      isOutput = false;
    else if (mode=="out")
      isOutput = true;
  }
  // basically act as black device so we can configure colors
  primaryGroup = group_black_joker;
  if (isOutput) {
    // GPIO output as on/off switch
    indicatorOutput = IndicatorOutputPtr(new IndicatorOutput(gpioname.c_str(), inverted, false));
    // Simulate light device
    // - create one output
    LightBehaviourPtr l = LightBehaviourPtr(new LightBehaviour(*this,outputs.size()));
    l->setHardwareDimmer(false); // GPIOs are digital on/off
    outputs.push_back(l);
  }
  else {
    // GPIO input as button
    buttonInput = ButtonInputPtr(new ButtonInput(gpioname.c_str(), inverted));
    buttonInput->setButtonHandler(boost::bind(&GpioDevice::buttonHandler, this, _2, _3), true);
    // - create one button input
    ButtonBehaviourPtr b = ButtonBehaviourPtr(new ButtonBehaviour(*this,buttons.size()));
    b->setHardwareButtonType(0, buttonType_single, buttonElement_center, false);
    buttons.push_back(b);
  }
	deriveDSID();
}


void GpioDevice::buttonHandler(bool aNewState, MLMicroSeconds aTimestamp)
{
	ButtonBehaviourPtr b = boost::dynamic_pointer_cast<ButtonBehaviour>(buttons[0]);
	if (b) {
		b->buttonAction(aNewState);
	}
}



int16_t GpioDevice::getOutputValue(OutputBehaviour &aOutputBehaviour)
{
  if (aOutputBehaviour.getIndex()==0 && indicatorOutput)
    return indicatorOutput->isSet() ? 255 : 0;
  else
    return inherited::getOutputValue(aOutputBehaviour); // let superclass handle this
}



void GpioDevice::setOutputValue(OutputBehaviour &aOutputBehaviour, int16_t aValue, MLMicroSeconds aTransitionTime)
{
  if (aOutputBehaviour.getIndex()==0 && indicatorOutput) {
    indicatorOutput->set(aValue>0);
  }
  else
    return inherited::setOutputValue(aOutputBehaviour, aValue); // let superclass handle this
}



void GpioDevice::deriveDSID()
{
  Fnv64 hash;
	
	// we have no unqiquely defining device information, construct something as reproducible as possible
	// - use class container's ID
	string s = classContainerP->deviceClassContainerInstanceIdentifier();
	hash.addBytes(s.size(), (uint8_t *)s.c_str());
	// - add-in the GPIO name
  if (buttonInput)
    hash.addCStr(buttonInput->getName());
  if (indicatorOutput)
    hash.addCStr(indicatorOutput->getName());
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


string GpioDevice::description()
{
  string s = inherited::description();
  if (buttonInput)
    string_format_append(s, "- Button at GPIO %s\n", buttonInput->getName());
  if (indicatorOutput)
    string_format_append(s, "- On/Off Lamp at GPIO %s\n", indicatorOutput->getName());
  return s;
}
