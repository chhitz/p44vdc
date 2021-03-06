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

#ifndef __p44vdc__binaryinputbehaviour__
#define __p44vdc__binaryinputbehaviour__

#include "device.hpp"

using namespace std;

namespace p44 {

  typedef uint8_t InputState;


  /// Implements the behaviour of a digitalSTROM binary input
  /// This class should be used as-is in virtual devices representing binary inputs
  class BinaryInputBehaviour : public DsBehaviour,  public ValueSource 
  {
    typedef DsBehaviour inherited;
    friend class Device;

  protected:

    /// @name behaviour description, constants or variables
    ///   set by device implementations when adding a Behaviour.
    /// @{
    DsBinaryInputType hardwareInputType; ///< the input type when device has hardwired functions
    VdcUsageHint inputUsage; ///< the input type when device has hardwired functions
    bool reportsChanges; ///< set if the input detects changes without polling
    MLMicroSeconds updateInterval;
    /// @}

    /// @name persistent settings
    /// @{
    DsGroup binInputGroup; ///< group this binary input belongs to
    DsBinaryInputType configuredInputType; ///< the configurable input type (aka Sensor Function)
    MLMicroSeconds minPushInterval; ///< minimum time between two state pushes
    MLMicroSeconds changesOnlyInterval; ///< time span during which only actual value changes are reported. After this interval, next hardware sensor update, even without value change, will cause a push)
    /// @}


    /// @name internal volatile state
    /// @{
    InputState currentState; ///< current input value
    MLMicroSeconds lastUpdate; ///< time of last update from hardware
    MLMicroSeconds lastPush; ///< time of last push
    /// @}


  public:

    /// constructor
    BinaryInputBehaviour(Device &aDevice);

    /// initialisation of hardware-specific constants for this binary input
    /// @param aInputType the input type (also called sensor function in classic dS)
    /// @param aUsage how this input is normally used (indoor/outdoor etc.)
    /// @param aReportsChanges if set, changes of this input can trigger a push. Inputs that do not have this flag set need
    ///   to be polled to get the input state. (At this time, this is descriptive only, and has no functionality within p44vdc)
    /// @param aUpdateInterval how often an update can be expected from this input. If 0, this means that no minimal
    ///   update interval can be expected.
    /// @note this must be called once before the device gets added to the device container. Implementation might
    ///   also derive default values for settings from this information.
    void setHardwareInputConfig(DsBinaryInputType aInputType, VdcUsageHint aUsage, bool aReportsChanges, MLMicroSeconds aUpdateInterval);

    /// get the hardware input type
    /// @return the type of the input
    DsBinaryInputType getHardwareInputType() { return hardwareInputType; };

    /// set group
    virtual void setGroup(DsGroup aGroup) { binInputGroup = aGroup; };

    /// @name interface towards actual device hardware (or simulation)
    /// @{

    /// invalidate input state, i.e. indicate that current state is not known
    void invalidateInputState();

    /// action occurred
    /// @param aNewState the new state of the input
    /// @note input can be a bool (0=false, 1=true) but can also have "extended values">1.
    ///   All extended values>0 are mapped to binaryInputState.value==true, and additionally
    ///   represented 1:1 in binaryInputState.extendedValue. For true binary inputs with only
    ///   2 states, binaryInputState.extendedValue is invisible.
    void updateInputState(InputState aNewState);

    /// @}

    /// check for defined state
    /// @return true if behaviour has a defined (non-NULL) state
    virtual bool hasDefinedState();


    /// @name ValueSource interface
    /// @{

    /// get descriptive name identifying the source within the entire vdc host (for using in selection lists)
    virtual string getSourceName();

    /// get value
    virtual double getSourceValue();

    /// get time of last update
    virtual MLMicroSeconds getSourceLastUpdate();

    /// @}



    /// description of object, mainly for debug and logging
    /// @return textual description of object, may contain LFs
    virtual string description();

  protected:

    /// the behaviour type
    virtual BehaviourType getType() { return behaviour_binaryinput; };

    /// returns the max extendedValue (depends on configuredInputType)
    /// @return max value the state is allowed to have. Normal binary inputs return 1 here, special cases like
    ///   binInpType_windowHandle might have extended values >1 to differentiate state.
    /// @note if this method returns >1, the behaviour will have an additional binaryInputState.extendedValue property
    InputState maxExtendedValue();


    // property access implementation for descriptor/settings/states
    virtual int numDescProps();
    virtual const PropertyDescriptorPtr getDescDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor);
    virtual int numSettingsProps();
    virtual const PropertyDescriptorPtr getSettingsDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor);
    virtual int numStateProps();
    virtual const PropertyDescriptorPtr getStateDescriptorByIndex(int aPropIndex, PropertyDescriptorPtr aParentDescriptor);
    // combined field access for all types of properties
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<BinaryInputBehaviour> BinaryInputBehaviourPtr;
  
  
  
} // namespace p44

#endif /* defined(__p44vdc__binaryinputbehaviour__) */
