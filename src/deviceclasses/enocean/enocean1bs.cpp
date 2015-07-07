//
//  Copyright (c) 2013-2015 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of vdcd.
//
//  vdcd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  vdcd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with vdcd. If not, see <http://www.gnu.org/licenses/>.
//

#include "enocean1bs.hpp"

#include "enoceandevicecontainer.hpp"

#include "binaryinputbehaviour.hpp"

using namespace p44;


Enocean1bsHandler::Enocean1bsHandler(EnoceanDevice &aDevice) :
  EnoceanChannelHandler(aDevice)
{
}


EnoceanDevicePtr Enocean1bsHandler::newDevice(
  EnoceanDeviceContainer *aClassContainerP,
  EnoceanAddress aAddress,
  EnoceanSubDevice aSubDeviceIndex,
  EnoceanProfile aEEProfile, EnoceanManufacturer aEEManufacturer,
  bool aSendTeachInResponse
) {
  EepFunc func = EEP_FUNC(aEEProfile);
  EepType type = EEP_TYPE(aEEProfile);
  EnoceanDevicePtr newDev; // none so far
  // At this time, only the "single input contact" profile is defined in EEP: D5-00-01
  if (func==0x00 && type==0x01) {
    // single input contact, always consists of a single device
    if (aSubDeviceIndex<1) {
      // create device
      newDev = EnoceanDevicePtr(new EnoceanDevice(aClassContainerP));
      // standard device settings without scene table
      newDev->installSettings();
      // assign channel and address
      newDev->setAddressingInfo(aAddress, aSubDeviceIndex);
      // assign EPP information
      newDev->setEEPInfo(aEEProfile, aEEManufacturer);
      newDev->setFunctionDesc("single contact");
      // joker by default, we don't know what kind of contact this is
      newDev->setPrimaryGroup(group_black_joker);
      // create channel handler
      Enocean1bsHandlerPtr newHandler = Enocean1bsHandlerPtr(new Enocean1bsHandler(*newDev.get()));
      // create the behaviour
      BinaryInputBehaviourPtr bb = BinaryInputBehaviourPtr(new BinaryInputBehaviour(*newDev.get()));
      bb->setHardwareInputConfig(binInpType_none, usage_undefined, true, 15*Minute);
      bb->setGroup(group_black_joker); // joker by default
      bb->setHardwareName(newHandler->shortDesc());
      newHandler->behaviour = bb;
      // add channel to device
      newDev->addChannelHandler(newHandler);
    }
  }
  // return device (or empty if none created)
  return newDev;
}


// handle incoming data from device and extract data for this channel
void Enocean1bsHandler::handleRadioPacket(Esp3PacketPtr aEsp3PacketPtr)
{
  if (!aEsp3PacketPtr->eepHasTeachInfo()) {
    // only look at non-teach-in packets
    if (aEsp3PacketPtr->eepRorg()==rorg_1BS && aEsp3PacketPtr->radioUserDataLength()==1) {
      // only look at 1BS packets of correct length
      uint8_t data = aEsp3PacketPtr->radioUserData()[0];
      // report contact state to binaryInputBehaviour
      BinaryInputBehaviourPtr bb = boost::dynamic_pointer_cast<BinaryInputBehaviour>(behaviour);
      if (bb) {
        bb->updateInputState(data & 0x01); // Bit 0 is the contact
      }
    }
  }
}


string Enocean1bsHandler::shortDesc()
{
  return "Single Contact";
}
