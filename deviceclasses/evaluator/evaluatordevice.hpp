//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44vdc__evaluatordevice__
#define __p44vdc__evaluatordevice__

#include "device.hpp"

#if ENABLE_EVALUATORS

using namespace std;

namespace p44 {


  class EvaluatorVdc;
  class EvaluatorDevice;


  class EvaluatorDeviceSettings : public DeviceSettings
  {
    typedef DeviceSettings inherited;
    friend class EvaluatorDevice;

    string valueDefs; ///< mapping of variable names to ValueSources
    string onCondition; ///< expression that must evaluate to true for output to get active
    string offCondition; ///< expression that must evaluate to true for output to get inactive
    MLMicroSeconds minOnTime; ///< how long the on condition must be present before triggering the result change
    MLMicroSeconds minOffTime; ///< how long the on condition must be present before triggering the result change

  protected:

    EvaluatorDeviceSettings(Device &aDevice);

    // persistence implementation
    virtual const char *tableName();
    virtual size_t numFieldDefs();
    virtual const FieldDefinition *getFieldDef(size_t aIndex);
    virtual void loadFromRow(sqlite3pp::query::iterator &aRow, int &aIndex, uint64_t *aCommonFlagsP);
    virtual void bindToStatement(sqlite3pp::statement &aStatement, int &aIndex, const char *aParentIdentifier, uint64_t aCommonFlags);

  };
  typedef boost::intrusive_ptr<EvaluatorDeviceSettings> EvaluatorDeviceSettingsPtr;


  class EvaluatorDevice : public Device
  {
    typedef Device inherited;
    friend class EvaluatorVdc;

    long long evaluatorDeviceRowID; ///< the ROWID this device was created from (0=none)
    
    typedef enum {
      evaluator_unknown,
      evaluator_rocker, ///< output is a simulated two-way rocket button
      evaluator_input, ///< output is a dS binary input signal
      evaluator_internal ///< the device is not published to dS, can only be used as input for other evaluators 
    } EvaluatorType;

    EvaluatorType evaluatorType;
    string evaluatorID;

    /// active value sources
    typedef map<string, ValueSource *> ValueSourcesMap;
    ValueSourcesMap valueMap;
    long valueParseTicket;

    Tristate currentState;

    MLMicroSeconds conditionMetSince; ///< since when do we see condition permanently met
    bool onConditionMet; ///< true: conditionMetSince relates to ON-condition, false: conditionMetSince relates to OFF-condition
    long evaluateTicket;
    bool evaluating; ///< protection against cyclic references

    EvaluatorDeviceSettingsPtr evaluatorSettings() { return boost::dynamic_pointer_cast<EvaluatorDeviceSettings>(deviceSettings); };

  public:

    EvaluatorDevice(EvaluatorVdc *aVdcP, const string &aEvaluatorID, const string &aEvaluatorConfig);
    virtual ~EvaluatorDevice();
    
    /// check if device is public dS device (which should be registered with vdSM)
    /// @return true if device is registerable with vdSM
    virtual bool isPublicDS();

    /// device type identifier
		/// @return constant identifier for this type of device (one container might contain more than one type)
    virtual string deviceTypeIdentifier() const { return "evaluator"; };

    EvaluatorVdc &getEvaluatorVdc();

    /// check if device can be disconnected by software (i.e. Web-UI)
    /// @return true if device might be disconnectable (deletable) by the user via software (i.e. web UI)
    /// @note devices returning true here might still refuse disconnection on a case by case basis when
    ///   operational state does not allow disconnection.
    /// @note devices returning false here might still be disconnectable using disconnect() triggered
    ///   by vDC API "remove" method.
    virtual bool isSoftwareDisconnectable() { return true; };

    /// disconnect device. For static device, this means removing the config from the container's DB. Note that command line
    /// static devices cannot be disconnected.
    /// @param aForgetParams if set, not only the connection to the device is removed, but also all parameters related to it
    ///   such that in case the same device is re-connected later, it will not use previous configuration settings, but defaults.
    /// @param aDisconnectResultHandler will be called to report true if device could be disconnected,
    ///   false in case it is certain that the device is still connected to this and only this vDC
    virtual void disconnect(bool aForgetParams, DisconnectCB aDisconnectResultHandler);

    /// description of object, mainly for debug and logging
    /// @return textual description of object
    virtual string description();


    /// @name identification of the addressable entity
    /// @{

    /// @return human readable model name/short description
    virtual string modelName();

    /// Get icon data or name
    /// @param aIcon string to put result into (when method returns true)
    /// - if aWithData is set, binary PNG icon data for given resolution prefix is returned
    /// - if aWithData is not set, only the icon name (without file extension) is returned
    /// @param aWithData if set, PNG data is returned, otherwise only name
    /// @return true if there is an icon, false if not
    virtual bool getDeviceIcon(string &aIcon, bool aWithData, const char *aResolutionPrefix);

    /// @}

    /// initializes the physical device for being used
    /// @param aFactoryReset if set, the device will be inititalized as thoroughly as possible (factory reset, default settings etc.)
    /// @note this is called before interaction with dS system starts
    /// @note implementation should call inherited when complete, so superclasses could chain further activity
    virtual void initializeDevice(StatusCB aCompletedCB, bool aFactoryReset);

    /// device level API methods (p44 specific, JSON only, for debugging evaluators)
    virtual ErrorPtr handleMethod(VdcApiRequestPtr aRequest, const string &aMethod, ApiValuePtr aParams);

  protected:

    void deriveDsUid();

    // property access implementation
    virtual int numProps(int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual PropertyDescriptorPtr getDescriptorByIndex(int aPropIndex, int aDomain, PropertyDescriptorPtr aParentDescriptor);
    virtual bool accessField(PropertyAccessMode aMode, ApiValuePtr aPropValue, PropertyDescriptorPtr aPropertyDescriptor);

  private:

    void forgetValueDefs();
    void parseValueDefs();

    void dependentValueNotification(ValueSource &aValueSource, ValueListenerEvent aEvent);
    void evaluateConditions(Tristate aRefState);
    void changedConditions();

    /// expression evaluation
    Tristate evaluateBoolean(string aExpression);
    ErrorPtr evaluateDouble(string &aExpression, double &aResult);
    ErrorPtr evaluateExpression(const char * &aText, double &aValue, int aPrecedence);
    ErrorPtr evaluateTerm(const char * &aText, double &aValue);

  };
  typedef boost::intrusive_ptr<EvaluatorDevice> EvaluatorDevicePtr;

} // namespace p44

#endif // ENABLE_EVALUATORS
#endif // __p44vdc__evaluatordevice__
