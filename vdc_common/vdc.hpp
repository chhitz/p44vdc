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

#ifndef __p44vdc__vdc__
#define __p44vdc__vdc__

#include "p44vdc_common.hpp"

#include "vdchost.hpp"

#include "dsuid.hpp"

using namespace std;

namespace p44 {

  class VdcError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Initialize,
    } ErrorCodes;
    
    static const char *domain() { return "DeviceClass"; }
    virtual const char *getErrorDomain() const { return VdcError::domain(); };
    VdcError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };
	
	
	
  class Device;

  typedef boost::intrusive_ptr<Device> DevicePtr;


  class Vdc;
  typedef boost::intrusive_ptr<Vdc> VdcPtr;
  typedef std::vector<DevicePtr> DeviceVector;


  /// This is the base class for a "class" (usually: type of hardware) of virtual devices.
  /// In dS terminology, this object represents a vDC (virtual device connector).
  class Vdc : public PersistentParams, public DsAddressable
  {
    typedef DsAddressable inherited;
    typedef PersistentParams inheritedParams;

    int instanceNumber; ///< the instance number identifying this instance among other instances of this class
    int tag; ///< tag used to in self test failures for showing on LEDs
    long pairTicket; ///< used for pairing

    /// generic vdc flag word
    int vdcFlags;
    /// default dS zone ID
    int defaultZoneID;

  protected:
  
    DeviceVector devices; ///< the devices of this class

  public:

    /// @param aInstanceNumber index which uniquely (and as stable as possible) identifies a particular instance
    ///   of this class container. This is used when generating dsuids for devices that don't have their own
    ///   unique ID, by using a hashOf(DeviceContainer's id, vdcClassIdentifier(), aInstanceNumber)
    /// @param aVdcHostP device container this vDC is contained in
    /// @param aTag numeric tag for this device container (e.g. for blinking self test error messages)
    Vdc(int aInstanceNumber, VdcHost *aVdcHostP, int aTag);

    /// add this vDC to vDC host.
    void addVdcToVdcHost();

		/// initialize
		/// @param aCompletedCB will be called when initialisation is complete
		///   callback will return an error if initialisation has failed and the vDC is not functional
		/// @param aFactoryReset if set, also perform factory reset for data persisted for this vDC
    virtual void initialize(StatusCB aCompletedCB, bool aFactoryReset);
		
    /// @name persistence
    /// @{

		/// get the persistent data dir path
		/// @return full path to directory to save persistent data
		const char *getPersistentDataDir();

    /// get the tag
    int getTag() const { return tag; };

    /// get number of devices
    size_t getNumberOfDevices() const { return devices.size(); };
		
    /// @}
		
		
    /// @name identification
    /// @{

    /// deviceclass identifier
		/// @return constant identifier for this container class (no spaces, filename-safe)
    virtual const char *vdcClassIdentifier() const = 0;
		
    /// Instance number (to differentiate multiple vDC containers of the same class)
		/// @return instance index number
		int getInstanceNumber() const;

    /// @return URL for Web-UI (for access from local LAN)
    virtual string webuiURLString() { return getVdcHost().webuiURLString(); /* by default, return vDC host's config URL */ };

    /// get a sufficiently unique identifier for this class container
    /// @return ID that identifies this container running on a specific hardware
    ///   the ID should not be dependent on the software version
    ///   the ID must differ for each of multiple vDC containers run on the same hardware
    ///   the ID MUST change when same software runs on different hardware
    /// @note Current implementation derives this from the devicecontainer's dSUID,
    ///   the deviceClassIdentitfier and the instance number in the form "class:instanceIndex@devicecontainerDsUid"
    string vdcInstanceIdentifier() const;

    /// some containers (statically defined devices for example) should be invisible for the dS system when they have no
    /// devices.
    /// @return if true, this vDC should not be announced towards the dS system when it has no devices
    virtual bool invisibleWhenEmpty() { return false; }

    /// get user assigned name of the vDC container, or if there is none, a synthesized default name
    /// @return name string
    virtual string getName() P44_OVERRIDE;

    /// set user assignable name
    /// @param aName name of the addressable entity
    virtual void setName(const string &aName) P44_OVERRIDE;

    /// vdc level methods
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams) P44_OVERRIDE;


    /// @}


    /// @name device collection, learning/pairing, self test
    /// @{

    /// Rescan modes
    enum {
      rescanmode_none = 0, ///< no mode bits set -> no rescan supported at all
      rescanmode_incremental = 0x01, ///< incremental rescan supported
      rescanmode_normal = 0x02, ///< normal rescan supported
      rescanmode_exhaustive = 0x04 ///< exhaustive rescan supported
    };
    /// get supported rescan modes for this vDC. This indicates (usually to a web-UI) which
    /// of the flags to collectDevices() make sense for this vDC.
    /// @return a combination of rescanmode_xxx bits
    virtual int getRescanModes() const { return rescanmode_none; }; // by default, assume not rescannable

    /// collect devices from this vDCs for normal operation
    /// @param aCompletedCB will be called when device scan for this vDC has been completed
    /// @param aIncremental if set, search is only made for additional new devices. Disappeared devices
    ///   might not get detected this way
    /// @param aExhaustive if set, device search is made exhaustive (may include longer lasting procedures to
    ///   recollect lost devices, assign bus addresses etc.). Without this flag set, device search should
    ///   still be complete under normal conditions, but might sacrifice corner case detection for speed.
    /// @param aClearSettings if set, persistent settings of currently known devices will be deleted before
    ///   re-scanning for devices, which means devices will have default settings after collecting.
    ///   Note that this is mutually exclusive with aIncremental (incremental scan does not remove any devices,
    ///   and thus cannot remove any settings, either)
    virtual void collectDevices(StatusCB aCompletedCB, bool aIncremental, bool aExhaustive, bool aClearSettings) = 0;

    /// perform self test
    /// @param aCompletedCB will be called when self test is done, returning ok or error
    /// @note self will be called *instead* of collectDevices() but might need to do some form of
    ///   collecting devices to perform the test. It might do that by calling collectDevices(), but
    ///   must make sure NOT to modify or generate any persistent data for the class.
    virtual void selfTest(StatusCB aCompletedCB);

    /// Forget all previously collected devices
    /// @param aForget if set, all parameters stored for the device (if any) will be deleted.
    /// @note the device is not disconnected (=unlearned) by this, so it will re-appear when scanned again
    /// @note if vdc API client is connected, it will receive "vanish" messages for all devices
    virtual void removeDevices(bool aForget);

    /// set container learn mode
    /// @param aEnableLearning true to enable learning mode
    /// @param aDisableProximityCheck true to disable proximity check (e.g. minimal RSSI requirement for some radio devices)
    /// @param aOnlyEstablish set this to yes to only learn in, to no to only learn out or to undefined to allow both learn-in and out.
    /// @note learn events (new devices found or devices removed) must be reported by calling reportLearnEvent() on VdcHost.
    virtual void setLearnMode(bool aEnableLearning, bool aDisableProximityCheck, Tristate aOnlyEstablish) { /* NOP in base class */ }

    /// @}


    /// @name services for actual vDC controller implementations
    /// @{

    /// Add device collected from hardware side (bus scan, etc.)
    /// @param aDevice a device object which has a valid dSUID
    /// @return false if aDevice's dSUID is already known.
    /// @note if aDevice's dSUID is already known, it will *not* be added again. This facilitates
    ///   implementation of incremental collection of newly appeared devices (scanning entire bus,
    ///   known ones will just be ignored when encountered again)
    /// @note this can be called as part of a collectDevices scan, or when a new device is detected
    ///   by other means than a scan/collect operation
    virtual bool addDevice(DevicePtr aDevice);

    /// Remove device known no longer connected to the system (for example: explicitly unlearned EnOcean switch)
    /// @param aDevice a device object which has a valid dSUID
    /// @param aForget if set, all parameters stored for the device will be deleted. Note however that
    ///   the device is not disconnected (=unlearned) by this.
    virtual void removeDevice(DevicePtr aDevice, bool aForget = false);

		/// @}


    /// @name vdc level property persistence
    /// @{

    /// load parameters from persistent DB
    /// @note this is usually called from the device container when device is added (detected)
    ErrorPtr load();

    /// save unsaved parameters to persistent DB
    /// @note this is usually called from the device container in regular intervals
    ErrorPtr save();

    /// forget any parameters stored in persistent DB
    ErrorPtr forget();

    // load additional settings from files
    void loadSettingsFromFiles();

		/// @}


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable, language independent model name/short description
    /// @note base class will construct this from global product name and vdcModelSuffix()
    virtual string modelName() P44_OVERRIDE { return string_format("%s %s", getVdcHost().productName.c_str(), vdcModelSuffix().c_str()); }

    /// @return human readable model name/short description
    virtual string vdcModelSuffix() const = 0;

    /// @return unique ID for the functional model of this entity
    virtual string modelUID() P44_OVERRIDE;

    /// @return the entity type (one of dSD|vdSD|vDChost|vDC|dSM|vdSM|dSS|*)
    virtual const char *entityType() P44_OVERRIDE { return "vDC"; }

    /// @return hardware version string or NULL if none
    virtual string hardwareVersion() P44_OVERRIDE { return ""; }

    /// @return hardware GUID in URN format to identify hardware as uniquely as possible
    virtual string hardwareGUID() P44_OVERRIDE { return ""; }

    /// @return OEM GUID in URN format to identify hardware as uniquely as possible
    virtual string oemGUID() P44_OVERRIDE { return ""; }

    /// @return Vendor name for display purposes
    /// @note if not empty, value will be used by vendorId() default implementation to create vendorname:xxx URN schema id
    virtual string vendorName() P44_OVERRIDE;

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix) P44_OVERRIDE;

    /// @}


    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description() P44_OVERRIDE;

  protected:

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyDescriptorPtr getDescriptorByName(string aPropMatch, int &aStartIndex, int aDomain, PropertyAccessMode aMode, PropertyDescriptorPtr aParentDescriptor) P44_OVERRIDE;
    virtual PropertyContainerPtr getContainer(const PropertyDescriptorPtr &aPropertyDescriptor, int &aDomain) P44_OVERRIDE;
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor) P44_OVERRIDE;

    // persistence implementation
    virtual const char *tableName() P44_OVERRIDE;
    virtual size_t numFieldDefs() P44_OVERRIDE;
    virtual const FieldDefinition *getFieldDef(size_t aIndex) P44_OVERRIDE;
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP) P44_OVERRIDE;
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags) P44_OVERRIDE;

    // derive dSUID
    void deriveDsUid();

  private:

    void performPair(VdcApiRequestPtr aRequest, Tristate aEstablish, bool aDisableProximityCheck, MLMicroSeconds aTimeout);
    void pairingEvent(VdcApiRequestPtr aRequest, bool aLearnIn, ErrorPtr aError);
    void pairingTimeout(VdcApiRequestPtr aRequest);

  };

} // namespace p44

#endif /* defined(__p44vdc__vdc__) */
