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

#include "enocean1bs.hpp"

#if ENABLE_ENOCEAN

#include "enoceanvdc.hpp"
#include "binaryinputbehaviour.hpp"

using namespace p44;


// MARK: ===== Enocean1BSDevice

Enocean1BSDevice::Enocean1BSDevice(EnoceanVdc *aVdcP) :
inherited(aVdcP)
{
}


EnoceanDevicePtr Enocean1BSDevice::newDevice(
  EnoceanVdc *aVdcP,
  EnoceanAddress aAddress,
  EnoceanSubDevice &aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EepFunc func = EEP_FUNC(aEEProfile);
  EepType type = EEP_TYPE(aEEProfile);
  EnoceanDevicePtr newDev; // none so far
  // At this time, only the "single input contact" profile is defined in EEP: D5-00-01
  // Note: two variants exist, one with inverted contact signal (e.g. for window contacts)
  if (func==0x00 && type==0x01) {
    // single input contact, always consists of a single device
    if (aSubDeviceIndex<1) {
      // create device
      newDev = EnoceanDevicePtr(new Enocean1BSDevice(aVdcP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("single contact");
      // joker by default, we don't know what kind of contact this is
      newDev->setColorClass(class_black_joker);
      // create channel handler, EEP variant 1 means inverted state interpretation
      SingleContactHandlerPtr newHandler = SingleContactHandlerPtr(new SingleContactHandler(*newDev.get(), !(EEP_VARIANT(aEEProfile)==1)));
      // create the behaviour
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get()));
      bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, 15*Minute);
      bb->setGroup(group_black_variable); // joker by default
      bb->setHardwareName(newHandler->shortDesc());
      newHandler->behaviour = bb;
      // add channel to device
      newDev->addChannelHandler(newHandler);
      // count it
      aSubDeviceIndex++;
    }
  }
  // return device (or empty if none created)
  return newDev;
}


static const ProfileVariantEntry E1BSprofileVariants[] = {
  // single contact alternatives
  { 1, 0x00D50001, 0, "single contact" },
  { 1, 0x01D50001, 0, "single contact (inverted, e.g. for window contact)" },
  { 0, 0, 0, NULL } // terminator
};


const ProfileVariantEntry *Enocean1BSDevice::profileVariantsTable()
{
  return E1BSprofileVariants;
}



// MARK: ===== SingleContactHandler


SingleContactHandler::SingleContactHandler(EnoceanDevice &aDevice, bool aActiveState) :
  EnoceanChannelHandler(aDevice),
  activeState(aActiveState)
{
}


// handle incoming data from device and extract data for this channel
void SingleContactHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->radioHasTeachInfo()) {
    // only look at non-teach-in packets
    if (aEsp3PacketPtr->eepRorg()==rorg_1BS && aEsp3PacketPtr->radioUserDataLength()==1) {
      // only look at 1BS packets of correct length
      uint8_t data = aEsp3PacketPtr->radioUserData()[0];
      // report contact state to binaryInputBehaviour
      BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
      if (bb) {
        bb->updateInputState(((data & 0x01)!=0) == activeState); // Bit 0 is the contact, report straight or inverted depending on activeState
      }
    }
  }
}


string SingleContactHandler::shortDesc()
{
  return "Single Contact";
}


#endif // ENABLE_ENOCEAN

