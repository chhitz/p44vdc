//
//  enoceancomm.cpp
//  vdcd
//
//  Created by Lukas Zeller on 03.05.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#include "enoceancomm.hpp"


using namespace p44;


#pragma mark - ESP3 packet object

// enoceansender hex up:
// 55 00 07 07 01 7A F6 30 00 86 B8 1A 30 03 FF FF FF FF FF 00 C0

Esp3Packet::Esp3Packet() :
  payloadP(NULL)
{
  clear();
}


Esp3Packet::~Esp3Packet()
{
  clear();
}


void Esp3Packet::clear()
{
  clearData();
  memset(header, 0, sizeof(header));
  state = ps_syncwait;
}


void Esp3Packet::clearData()
{
  if (payloadP) {
    if (payloadP) delete [] payloadP;
    payloadP = NULL;
  }
  payloadSize = 0;
}



// ESP3 Header
//  0 : 0x55 sync byte
//  1 : data length MSB
//  2 : data length LSB
//  3 : optional data length
//  4 : packet type
//  5 : CRC over bytes 1..4

#define ESP3_HEADERBYTES 6



size_t Esp3Packet::dataLength()
{
  return (header[1]<<8) + header[2];
}

void Esp3Packet::setDataLength(size_t aNumBytes)
{
  header[1] = (aNumBytes>>8) & 0xFF;
  header[2] = (aNumBytes) & 0xFF;
}


size_t Esp3Packet::optDataLength()
{
  return header[3];
}

void Esp3Packet::setOptDataLength(size_t aNumBytes)
{
  header[3] = aNumBytes;
}


PacketType Esp3Packet::packetType()
{
  return (PacketType)header[4];
}


void Esp3Packet::setPacketType(PacketType aPacketType)
{
  header[4] = (uint8_t)aPacketType;
}


uint8_t Esp3Packet::headerCRC()
{
  return crc8(header+1, ESP3_HEADERBYTES-2);
}


uint8_t Esp3Packet::payloadCRC()
{
  if (!payloadP) return 0;
  return crc8(payloadP, payloadSize-1); // last byte of payload is CRC itself
}


bool Esp3Packet::isComplete()
{
  return state==ps_complete;
}



size_t Esp3Packet::acceptBytes(size_t aNumBytes, uint8_t *aBytes)
{
  size_t replayBytes = 0;
  size_t acceptedBytes = 0;
  uint8_t *replayP;
  // completed packets do not accept any more bytes
  if (state==ps_complete) return 0;
  // process bytes
  while (acceptedBytes<aNumBytes || replayBytes>0) {
    uint8_t byte;
    if (replayBytes>0) {
      // reconsider already stored byte
      byte = *replayP++;
      replayBytes--;
    }
    else {
      // process a new byte
      byte = *aBytes;
      // next
      aBytes++;
      acceptedBytes++;
    }
    switch (state) {
      case ps_syncwait:
        // waiting for 0x55 sync byte
        if (byte==0x55) {
          // potential start of packet
          header[0] = byte;
          // - start reading header
          state = ps_headerread;
          dataIndex = 1;
        }
        break;
      case ps_headerread:
        // collecting header bytes 1..5
        header[dataIndex] = byte;
        ++dataIndex;
        if (dataIndex==ESP3_HEADERBYTES) {
          // header including CRC received
          // - check header CRC now
          if (header[ESP3_HEADERBYTES-1]!=headerCRC()) {
            // CRC mismatch
            // - replay from byte 1 (which could be a sync byte again)
            replayP = header+1; // consider 2nd byte of already received and stored header as potential start
            replayBytes = ESP3_HEADERBYTES-1;
            // - back to syncwait
            state = ps_syncwait;
          }
          else {
            // CRC matches, now read data
            // - make sure we have a buffer according to dataLength() and optDataLength()
            data();
            dataIndex = 0; // start of data read
            // - enter payload read state
            state = ps_dataread;
          }
        }
        break;
      case ps_dataread:
        // collecting payload
        payloadP[dataIndex] = byte;
        ++dataIndex;
        if (dataIndex==payloadSize) {
          // payload including CRC received
          // - check payload CRC now
          if (payloadP[payloadSize-1]!=payloadCRC()) {
            // payload CRC mismatch, discard packet, start scanning for packet at next byte
            clear();
          }
          else {
            // packet is complete,
            state = ps_complete;
            // just return number of bytes accepted to complete it
            return acceptedBytes;
          }
        }
        break;
      default:
        // something's wrong, reset the packet
        clear();
        break;
    }
  }
  // number of bytes accepted (but packet not complete)
  return acceptedBytes;
}


uint8_t *Esp3Packet::data()
{
  size_t s = dataLength()+optDataLength()+1; // one byte extra for CRC
  if (s!=payloadSize || !payloadP) {
    if (payloadSize>300) {
      // safety - prevent huge telegrams
      clearData();
      return NULL;
    }
    payloadSize = s;
    if (payloadP) delete [] payloadP;
    payloadP = new uint8_t[payloadSize];
    memset(payloadP, 0, payloadSize); // zero out
  }
  return payloadP;
}


uint8_t *Esp3Packet::optData()
{
  uint8_t *o = data();
  if (o) {
    o += dataLength();
  }
  return o;
}



void Esp3Packet::finalize()
{
  // force creation of payload (usually already done, but to make sure to avoid crashes)
  data();
  // set sync byte
  header[0] = 0x55;
  // assign header CRC
  header[ESP3_HEADERBYTES-1] = headerCRC();
  // assign payload CRC
  if (payloadP) {
    payloadP[payloadSize-1] = payloadCRC();
  }
  // packet is complete now
  state = ps_complete;
}



#pragma mark - radio telegram specifics


// Radio telegram optional data
//  0    : Subtelegram Number, 3 for set, 1..n for receive
//  1..4 : destination address, FFFFFFFF = broadcast
//  5    : dBm, send: set to FF, receive: best RSSI value of all subtelegrams
//  6    : security level: 0 = unencrypted, 1..F = type of encryption


uint8_t Esp3Packet::radioSubtelegrams()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return o[0];
}


EnoceanAddress Esp3Packet::radioDestination()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return
    (o[1]<<24) +
    (o[2]<<16) +
    (o[3]<<8) +
    (o[4]);
}


void Esp3Packet::setRadioDestination(EnoceanAddress aEnoceanAddress)
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return;
  o[1] = (aEnoceanAddress>>24) & 0xFF;
  o[2] = (aEnoceanAddress>>16) & 0xFF;
  o[3] = (aEnoceanAddress>>8) & 0xFF;
  o[4] = aEnoceanAddress & 0xFF;
}



int Esp3Packet::radioDBm()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return -o[5];
}


uint8_t Esp3Packet::radioSecurityLevel()
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return 0;
  return o[6];
}


void Esp3Packet::setRadioSecurityLevel(uint8_t aSecLevel)
{
  uint8_t *o = optData();
  if (!o || optDataLength()<7) return;
  o[6] = aSecLevel;
}


uint8_t Esp3Packet::radioStatus()
{
  RadioOrg rorg = eepRorg();
  int statusoffset = 0;
  if (rorg!=rorg_invalid) {
    statusoffset = (int)dataLength()-1; // last byte is status...
    if (rorg==rorg_VLD) statusoffset--; // ..except for VLD, where last byte is CRC
  }
  if (statusoffset<0) return 0;
  return data()[statusoffset]; // this is the status byte
}




size_t Esp3Packet::radioUserDataLength()
{
  if (packetType()!=pt_radio) return 0; // no data
  RadioOrg rorg = eepRorg();
  int bytes = (int)dataLength(); // start with actual length
  bytes -= 1; // RORG byte
  bytes -= 1; // one status byte
  bytes -= 4; // 4 bytes for sender
  if (rorg==rorg_VLD) bytes -= 1; // extra CRC
  return bytes<0 ? 0 : bytes;
}


void Esp3Packet::setRadioUserDataLength(size_t aSize)
{
  if (packetType()!=pt_radio) return; // is not radio packet
  RadioOrg rorg = eepRorg();
  // add extra length needed for fixed fields in radio packet
  aSize += 1; // RORG byte
  aSize += 1; // one status byte
  aSize += 4; // 4 bytes for sender
  if (rorg==rorg_VLD) aSize += 1; // extra CRC
  // this is the complete data length
  setDataLength(aSize);
}



uint8_t *Esp3Packet::radioUserData()
{
  if (radioUserDataLength()==0) return NULL;
  uint8_t *d = data();
  return d+1;
}


EnoceanAddress Esp3Packet::radioSender()
{
  size_t l = radioUserDataLength(); // returns 0 for non-radio packets
  if (l>0) {
    uint8_t *d = data()+1+l; // skip RORG and userdata
    return
      (d[0]<<24) +
      (d[1]<<16) +
      (d[2]<<8) +
      d[3];
  }
  else
    return 0;
}


void Esp3Packet::setRadioSender(EnoceanAddress aEnoceanAddress)
{
  size_t l = radioUserDataLength(); // returns 0 for non-radio packets
  if (l>0) {
    uint8_t *d = data()+1+l; // skip RORG and userdata
    d[0] = (aEnoceanAddress>>24) & 0xFF;
    d[1] = (aEnoceanAddress>>16) & 0xFF;
    d[2] = (aEnoceanAddress>>8) & 0xFF;
    d[3] = aEnoceanAddress & 0xFF;
  }
}




void Esp3Packet::initForRorg(RadioOrg aRadioOrg, size_t aVLDsize)
{
  clear(); // init
  // set as radio telegram
  setPacketType(pt_radio);
  // radio telegrams always have 7 fields of optional data
  setOptDataLength(7);
  // depending on radio org, set payload size
  switch (aRadioOrg) {
    case rorg_RPS:
    case rorg_1BS:
      setRadioUserDataLength(1);
      break;
    case rorg_4BS:
      setRadioUserDataLength(4);
      break;
    case rorg_VLD:
      if (aVLDsize>14) aVLDsize=14; // limit to max
      else if (aVLDsize<1) aVLDsize=1; // limit to min
      setRadioUserDataLength(aVLDsize);
      break;
    default:
      break;
  }
  // set the radio org
  data()[0] = aRadioOrg;
  // now set optional data defaults
  uint8_t *o = optData();
  // - subTelegramNo for sending is always 3
  o[0] = 3;
  // - dBm for sending is always 0xFF
  o[5] = 0xFF;
  // default to no security
  setRadioSecurityLevel(0);
}



#pragma mark - Enocean Eqipment Profile (EEP) information extraction


// Radio telegram data
//  0        : RORG
//  1..n     : user data, n bytes
//  n+1..n+4 : sender address
//  n+5      : status
//  n+6      : for VLD only: CRC

RadioOrg Esp3Packet::eepRorg()
{
  if (packetType()!=pt_radio) return rorg_invalid; // no radio
  uint8_t *d = data();
  if (!d || dataLength()<1) return rorg_invalid; // no RORG
  return (RadioOrg)d[0]; // this is the RORG byte
}



// RPS Signatures
//
// Status                D[0]
// T21 NU    7   6   5   4   3   2   1   0    RORG FUNC TYPE   Desc       Notes
// --- --   --- --- --- --- --- --- --- ---   ---- ---- ----   ---------- -------------------
//  1   0    1   x   x   x   x   x   x   x    F6   10   00     Win Handle SIGNATURE
//
//  1   x    0   x   x   x   x   x   x   x    F6   02   01/2   2-Rocker   SIGNATURE (not unique, overlaps with key card switch)
//
//  0   x    x   x   x   x   x   x   x   x    F6   03   01/2   4-Rocker   SIGNATURE
//
//
//  1   x    0   x   x   x   0   0   0   0    F6   04   01     Key Card   no unqiue SIGNATURE (overlaps with 2-Rocker)


// 1BS Telegrams
//
//                       D[0]
// T21 NU    7   6   5   4   3   2   1   0    RORG FUNC TYPE   Desc       Notes
// --- --   --- --- --- --- --- --- --- ---   ---- ---- ----   ---------- -------------------
//  x   x    x   x   x   x  LRN  x   x   c    D5   00   01     1 Contact  c:0=open,1=closed


// 4BS teach-in telegram
//
//       D[0]      |       D[1]      |       D[2]      |              D[3]
// 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 |  7   6   5   4   3   2   1   0
//
// f f f f f f t t   t t t t t m m m   m m m m m m m m   LRN EEP LRN LRN LRN  x   x   x
//    FUNC    |     TYPE      |      MANUFACTURER      | typ res res sta bit


// SA_LEARN_REQUEST
//
//    D[0]     D[1]   D[2] D[3] D[4] D[5] D[6] D[7] D[8] D[9] D[10] D[11] D[12] D[13] D[14] D[15]
//  rrrrrmmm mmmmmmmm RORG FUNC TYPE RSSI ID3  ID2  ID1  ID0   ID3   ID2   ID1   ID0  STAT  CHECK
//  Req  Manufacturer|   EEP No.    |dBm |    Repeater ID    |       Sender ID       |     |


EnoceanProfile Esp3Packet::eepProfile()
{
  // default: unknown signature
  EnoceanProfile profile = eep_profile_unknown;
  RadioOrg rorg = eepRorg();
  if (rorg!=rorg_invalid) {
    // valid rorg
    if (rorg==rorg_RPS) {
      // RPS have no learn mode, EEP signature can be derived from bits (not completely, but usable approximation)
      uint8_t status = radioStatus();
      uint8_t data = radioUserData()[0];
      if ((status & status_T21)!=0) {
        // Win handle or 2-Rocker (or key card, but we can't distinguish that, so we default to 2-Rocker)
        if ((data & 0x80)!=0 && (status & status_NU)==0) {
          // Window handle
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x10<<8) | (0x00); // FUNC = Window handle, TYPE = 0 (no others defined)
        }
        else if ((data & 0x80)==0) {
          // 2-Rocker (or key card, but we ignore that
          profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x02<<8) | (eep_func_unknown); // FUNC = 2-Rocker switch, type unknown (1 or 2 is possible)
        }
      }
      else {
        // must be 4-Rocker
        profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x03<<8) | (eep_func_unknown); // FUNC = 4-Rocker switch, type unknown (1 or 2 is possible)
      }
    }
    else if (rorg==rorg_1BS) {
      // 1BS has a learn bit
      if (eepHasTeachInfo()) {
        // As per March 2013, only one EEP is defined for 1BS: single contact
        profile = ((EnoceanProfile)rorg<<16) | ((EnoceanProfile)0x00<<8) | (0x01); // FUNC = contacts and switches, TYPE = single contact
      }
    }
    else if (rorg==rorg_4BS) {
      // 4BS has separate LRN telegrams
      if (eepHasTeachInfo()) {
        profile =
          (rorg<<16) |
          (((EnoceanProfile)(radioUserData()[0])<<6) & 0x3F00) | // 6 FUNC bits, shifted to bit 8..13
          (((EnoceanProfile)(radioUserData()[0])<<5) & 0x60) | // upper 2 TYPE bits, shifted to bit 5..6
          (((EnoceanProfile)(radioUserData()[1])>>3) & 0x1F); // lower 5 TYPE bits, shifted to bit 0..4
      }
    }
    else if (rorg==rorg_SM_LRN_REQ) {
      // Smart Ack Learn Request
      profile =
        (((EnoceanProfile)radioUserData()[2])<<16) | // RORG field
        (((EnoceanProfile)radioUserData()[3])<<8) | // FUNC field
        radioUserData()[4]; // TYPE field
    }
  } // valid rorg
  // return it
  return profile;
}


EnoceanManufacturer Esp3Packet::eepManufacturer()
{
  EnoceanManufacturer man = manufacturer_unknown;
  RadioOrg rorg = eepRorg();
  if (eepHasTeachInfo()) {
    if (rorg==rorg_4BS) {
      man =
        ((((EnoceanManufacturer)radioUserData()[1])<<8) & 0x07) |
        radioUserData()[2];
    }
    else if (rorg==rorg_SM_LRN_REQ) {
      man =
        ((((EnoceanManufacturer)radioUserData()[0])&0x07)<<8) |
        radioUserData()[1];
    }
  }
  // return it
  return man;
}



bool Esp3Packet::eepHasTeachInfo(int aMinLearnDBm, bool aMinDBmForAll)
{
  RadioOrg rorg = eepRorg();
  bool radioStrengthSufficient = aMinLearnDBm==0 || radioDBm()>aMinLearnDBm;
  bool explicitLearnOK = !aMinDBmForAll || radioStrengthSufficient; // ok if no restriction on radio strength OR strength sufficient
  switch (rorg) {
    case rorg_RPS:
      return radioStrengthSufficient; // RPS telegrams always have (somewhat limited) signature that can be used for teach-in
    case rorg_1BS:
      return ((radioUserData()[0] & LRN_BIT_MASK)==0) && explicitLearnOK; // 1BS telegrams have teach-in info if LRN bit is *cleared*
    case rorg_4BS:
      return ((radioUserData()[3] & LRN_BIT_MASK)==0) && explicitLearnOK; // 4BS telegrams have teach-in info if LRN bit is *cleared*
    case rorg_SM_LRN_REQ:
      return explicitLearnOK; // smart ack learn requests are by definition teach-in commands and have full EEP signature
    default:
      return false; // no learn-in, regular data 
  }
}


#pragma mark - 4BS comminication specifics


uint32_t Esp3Packet::get4BSdata()
{
  if (eepRorg()==rorg_4BS) {
    return
      (radioUserData()[0]<<24) |
      (radioUserData()[1]<<16) |
      (radioUserData()[2]<<8) |
      radioUserData()[3];
  }
  return 0;
}


void Esp3Packet::set4BSdata(uint32_t a4BSdata)
{
  if (eepRorg()==rorg_4BS) {
    radioUserData()[0] = (a4BSdata>>24) & 0xFF;
    radioUserData()[1] = (a4BSdata>>16) & 0xFF;
    radioUserData()[2] = (a4BSdata>>8) & 0xFF;
    radioUserData()[3] = a4BSdata & 0xFF;
  }
}


// 4BS teach-in telegram
//
//       D[0]      |       D[1]      |       D[2]      |              D[3]
// 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 | 7 6 5 4 3 2 1 0 |  7   6   5   4   3   2   1   0
//
// f f f f f f t t   t t t t t m m m   m m m m m m m m   LRN EEP LRN LRN LRN  x   x   x
//    FUNC    |     TYPE      |      MANUFACTURER      | typ res res sta bit


void Esp3Packet::set4BSTeachInEEP(EnoceanProfile aEEProfile)
{
  if (eepRorg()==rorg_4BS && EEP_RORG(aEEProfile)==rorg_4BS) {
    radioUserData()[0] =
      ((aEEProfile>>6) & 0xFC) | // 6 FUNC bits
      ((aEEProfile>>5) & 0x03); // upper 2 TYPE bits
    radioUserData()[1] =
      ((aEEProfile<<3) & 0x1F); // lower 5 TYPE bits
  }
}




#pragma mark - Description


string Esp3Packet::description()
{
  if (isComplete()) {
    string t;
    if (packetType()==pt_radio) {
      // ESP3 radio packet
      string_format_append(t,
        "ESP3 RADIO rorg=0x%02X,  sender=0x%08lX, status=0x%02X\n"
        "- subtelegrams=%d, destination=0x%08lX, dBm=%d, secLevel=%d\n",
        eepRorg(),
        radioSender(),
        radioStatus(),
        radioSubtelegrams(),
        radioDestination(),
        radioDBm(),
        radioSecurityLevel()
      );
      // EEP info if any
      if (eepHasTeachInfo()) {
        string_format_append(t,
          "- Is Learn-In packet: EEP RORG/FUNC/TYPE: %02X %02X %02X, Manufacturer = %s (%03X)\n",
          (eepProfile()>>16) & 0xFF,
          (eepProfile()>>8) & 0xFF,
          eepProfile() & 0xFF,
          EnoceanComm::manufacturerName(eepManufacturer()),
          eepManufacturer()
        );
      }
    }
    else if (packetType()==pt_response) {
      // non-radio ESP3 packet
      string_format_append(t, "ESP3 response packet, return code = %d\n", data()[0]);
    }
    // raw data
    string_format_append(t, "- %3d data bytes: ", dataLength());
    for (int i=0; i<dataLength(); i++)
      string_format_append(t, "%02X ", data()[i]);
    t.append("\n");
    if (packetType()==pt_radio) {
      string_format_append(t, "- %3d opt  bytes: ", optDataLength());
      for (int i=0; i<optDataLength(); i++)
        string_format_append(t, "%02X ", optData()[i]);
      t.append("\n");
    }
    return t;
  }
  else {
    return string_format("Incomplete ESP3 packet in state = %d\n", (int)state);
  }
}



#pragma mark - CRC8 calculation

static u_int8_t CRC8Table[256] = {
  0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
  0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
  0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
  0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
  0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
  0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
  0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
  0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
  0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
  0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
  0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
  0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
  0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
  0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
  0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
  0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
  0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
  0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
  0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
  0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
  0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
  0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
  0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
  0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
  0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
  0x76, 0x71, 0x78, 0x7f, 0x6A, 0x6d, 0x64, 0x63,
  0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
  0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
  0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
  0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8D, 0x84, 0x83,
  0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
  0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
};


uint8_t Esp3Packet::addToCrc8(uint8_t aByte, uint8_t aCRCValue)
{
  return CRC8Table[aCRCValue ^ aByte];
}


uint8_t Esp3Packet::crc8(uint8_t *aDataP, size_t aNumBytes, uint8_t aCRCValue)
{
  int i;
  for (i = 0; i<aNumBytes; i++) {
    aCRCValue = addToCrc8(aCRCValue, aDataP[i]);
  }
  return aCRCValue;
}



#pragma mark - Manufacturer names

typedef struct {
  EnoceanManufacturer manufacturerID;
  const char *name;
} EnoceanManufacturerDesc;


static const EnoceanManufacturerDesc manufacturerDescriptions[] = {
  { 0x000, "Manufacturer Reserved" },
  { 0x001, "Peha" },
  { 0x002, "Thermokon" },
  { 0x003, "Servodan" },
  { 0x004, "EchoFlex Solutions" },
  { 0x005, "Omnio AG" },
  { 0x006, "Hardmeier electronics" },
  { 0x007, "Regulvar Inc" },
  { 0x008, "Ad Hoc Electronics" },
  { 0x009, "Distech Controls" },
  { 0x00A, "Kieback + Peter" },
  { 0x00B, "EnOcean GmbH" },
  { 0x00C, "Probare" },
  { 0x00D, "Eltako" },
  { 0x00E, "Leviton" },
  { 0x00F, "Honeywell" },
  { 0x010, "Spartan Peripheral Devices" },
  { 0x011, "Siemens" },
  { 0x012, "T-Mac" },
  { 0x013, "Reliable Controls Corporation" },
  { 0x014, "Elsner Elektronik GmbH" },
  { 0x015, "Diehl Controls" },
  { 0x016, "BSC Computer" },
  { 0x017, "S+S Regeltechnik GmbH" },
  { 0x018, "Masco Corporation" },
  { 0x019, "Intesis Software SL" },
  { 0x01A, "Viessmann" },
  { 0x01B, "Lutuo Technology" },
  { 0x01C, "CAN2GO" },
  { 0x01D, "Sauter" },
  { 0x01E, "Boot-Up"  },
  { 0x01F, "Osram Sylvania"  },
  { 0x020, "Unotech"  },
  { 0x022, "Unitronic AG" },
  { 0x023, "NanoSense" },
  { 0x024, "The S4 Ggroup" },
  { 0x025, "MSR Solutions " },
  { 0x027, "Maico" },
  { 0x02A, "KM Controls" },
  { 0x02B, "Ecologix Controls" },
  { 0x02D, "Afriso Euro Index" },
  { 0x030, "NEC AccessTechnica Ltd" },
  { 0x031, "ITEC Corporation" },
  { 0x7FF, "Multi user Manufacturer ID" },
  { 0, NULL /* NULL string terminates list */ }
};



const char *EnoceanComm::manufacturerName(EnoceanManufacturer aManufacturerCode)
{
  const EnoceanManufacturerDesc *manP = manufacturerDescriptions;
  while (manP->name) {
    if (manP->manufacturerID==aManufacturerCode) {
      return manP->name;
    }
    manP++;
  }
  // none found
  return "<unknown>";
}



#pragma mark - EnOcean communication handler

// pseudo baudrate for dali bridge must be 9600bd
#define ENOCEAN_ESP3_BAUDRATE 57600


EnoceanComm::EnoceanComm(SyncIOMainLoop *aMainLoopP) :
	inherited(aMainLoopP)
{
}


EnoceanComm::~EnoceanComm()
{
}


void EnoceanComm::setConnectionParameters(const char* aConnectionPath, uint16_t aPortNo)
{
  inherited::setConnectionParameters(aConnectionPath, aPortNo, ENOCEAN_ESP3_BAUDRATE);
	// open connection so we can receive
	establishConnection();
}


void EnoceanComm::setRadioPacketHandler(RadioPacketCB aRadioPacketCB)
{
  radioPacketHandler = aRadioPacketCB;
}




size_t EnoceanComm::acceptBytes(size_t aNumBytes, uint8_t *aBytes)
{
	size_t remainingBytes = aNumBytes;
	while (remainingBytes>0) {
		if (!currentIncomingPacket) {
			currentIncomingPacket = Esp3PacketPtr(new Esp3Packet);
		}
		// pass bytes to current telegram
		size_t consumedBytes = currentIncomingPacket->acceptBytes(remainingBytes, aBytes);
		if (currentIncomingPacket->isComplete()) {
      LOG(LOG_INFO, "Received Enocean Packet:\n%s", currentIncomingPacket->description().c_str());
      dispatchPacket(currentIncomingPacket);
      // forget the packet, further incoming bytes will create new packet
			currentIncomingPacket = Esp3PacketPtr(); // forget
		}
		// continue with rest (if any)
		aBytes+=consumedBytes;
		remainingBytes-=consumedBytes;
	}
	return aNumBytes-remainingBytes;
}


void EnoceanComm::dispatchPacket(Esp3PacketPtr aPacket)
{
  PacketType pt = aPacket->packetType();
  if (pt==pt_radio) {
    // incoming radio packet
    if (radioPacketHandler) {
      // call the handler
      radioPacketHandler(this, aPacket, ErrorPtr());
    }
  }
  else if (pt==pt_response) {
    // TODO: %%% have queue check for operations awaiting a response command
  }
  else {
    // TODO: %%% handle other packet types
  }
}



void EnoceanComm::sendPacket(Esp3PacketPtr aPacket)
{
  // finalize, calc CRC
  aPacket->finalize();
  // transmit
  // - fixed header
  transmitBytes(ESP3_HEADERBYTES, aPacket->header);
  // - payload
  transmitBytes(aPacket->payloadSize, aPacket->payloadP);
}





