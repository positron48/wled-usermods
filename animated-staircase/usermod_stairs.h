/*
 * Usermod for detecting people entering/leaving a staircase and switching the
 * staircase lights on/off.
 *
 * Edit the Animated_Staircase_config.h file to compile this usermod for your
 * specific configuration.
 */
#pragma once
#include "wled.h"

// Define the Animated_Staircase class which inherits from Usermod
class Animated_Staircase : public Usermod {
  private:

    /* Configuration variables (accessible via API and stored in flash memory) */
    bool enabled = false;                   // Enable or disable this usermod
    unsigned long segment_delay_ms = 150;   // Delay between switching each segment of the staircase (in milliseconds)
    unsigned long on_time_ms       = 10000; // Duration for which the staircase lights stay on (in milliseconds)
    int8_t topPIRorTriggerPin      = -1;    // Pin for the top PIR sensor or trigger, -1 means disabled
    int8_t bottomPIRorTriggerPin   = -1;    // Pin for the bottom PIR sensor or trigger, -1 means disabled
    int8_t enableSwitchPin         = -1;    // Pin for the hardware switch to enable/disable the usermod, -1 means disabled
    bool togglePower               = false; // Toggle power on/off with the staircase lights

    /* Runtime variables */
    bool initDone = false;

    // Constant delay for sensor checking (in milliseconds)
    const unsigned int scanDelay = 100;

    // Lights on or off.
    // Flipping this will start a transition.
    bool on = false;

    // Tracks the last sensor activated to determine the direction of movement
    #define LOWER false
    #define UPPER true
    bool lastSensor = LOWER; // Last activated sensor
    bool swipe = LOWER;      // Direction of swipe (UPPER or LOWER)

    // Timestamp of the last transition action (in milliseconds)
    unsigned long lastTime = 0;

    // Timestamp of the last sensor check (in milliseconds)
    unsigned long lastScanTime = 0;

    // Timestamp of the last light switch action (in milliseconds)
    unsigned long lastSwitchTime = 0;

    // Indices for controlling the segments (light sections) of the staircase
    int8_t topIndex = 0;       // Index for the top of the staircase
    int8_t bottomIndex = 0;    // Index for the bottom of the staircase
    int8_t disableIndex = 0;   // Index for disabling segments

    // Maximum and minimum segment IDs for the configured staircase
    byte maxSegmentId = 1;
    byte minSegmentId = 0;

    // Variables to store the state of sensors and switches, used by the API
    bool topSensorRead     = false;
    bool topSensorWrite    = false;
    bool bottomSensorRead  = false;
    bool bottomSensorWrite = false;
    bool enableSwitchRead  = false;
    bool enableSwitchWrite = false;
    bool topSensorState    = false;
    bool bottomSensorState = false;
    bool enableSwitchState = false;

    // Strings used multiple times in the code to save flash memory
    static const char _name[];
    static const char _enabled[];
    static const char _segmentDelay[];
    static const char _onTime[];
    static const char _topPIRorTrigger_pin[];
    static const char _bottomPIRorTrigger_pin[];
    static const char _enableSwitch_pin[];
    static const char _togglePower[];

    // Function to publish sensor states to MQTT
    void publishMqtt(bool bottom, const char* state) {
#ifndef WLED_DISABLE_MQTT
      // Check if MQTT is connected to prevent crashing
      if (WLED_MQTT_CONNECTED){
        char subuf[64];
        // Format the topic string for publishing
        sprintf_P(subuf, PSTR("%s/motion/%d"), mqttDeviceTopic, (int)bottom);
        mqtt->publish(subuf, 0, false, state);  // Publish the sensor state to MQTT
      }
#endif
    }

    // Function to update the staircase light segments based on sensor input
    void updateSegments() {
      // Update segments from the top of the staircase
      if (topIndex < maxSegmentId && topIndex >= minSegmentId) {
          strip.getSegment(topIndex).setOption(SEG_OPTION_ON, true);
          topIndex++;
      }

      // Update segments from the bottom of the staircase
      if (bottomIndex >= minSegmentId && bottomIndex < maxSegmentId) {
          strip.getSegment(bottomIndex).setOption(SEG_OPTION_ON, true);
          bottomIndex--;
      }

      // If a swipe is detected, disable segments accordingly
      if (swipe) {
        if (disableIndex >= bottomIndex && bottomIndex != -1) {
          disableIndex = maxSegmentId;
        }
        if (disableIndex < maxSegmentId && disableIndex >= 0) {
          strip.getSegment(disableIndex).setOption(SEG_OPTION_ON, false);
          disableIndex++;
        }
      } else {
        if (disableIndex <= topIndex && topIndex != maxSegmentId) {
          disableIndex = -1;
        }
        if (disableIndex >= minSegmentId && disableIndex < maxSegmentId) {
          strip.getSegment(disableIndex).setOption(SEG_OPTION_ON, false);
          disableIndex--;
        }
      }

      strip.trigger();  // Force refresh of the light strip
      stateChanged = true;  // Notify external devices/UI of the state change
      colorUpdated(CALL_MODE_DIRECT_CHANGE);  // Update the color to reflect changes
    }

    // Function to check the state of sensors and handle sensor changes
    bool checkSensors() {
      bool sensorChanged = false;

      // Check sensors only if enough time has passed since the last check
      if ((millis() - lastScanTime) > scanDelay) {
        lastScanTime = millis();

        // Read the state of the bottom and top sensors and the enable switch
        bottomSensorRead = bottomSensorWrite || (bottomPIRorTriggerPin<0 ? false : digitalRead(bottomPIRorTriggerPin));
        topSensorRead = topSensorWrite || (topPIRorTriggerPin<0 ? false : digitalRead(topPIRorTriggerPin));
        enableSwitchRead = enableSwitchWrite || (enableSwitchPin<0 ? false : digitalRead(enableSwitchPin));

        // Check if the state of the bottom sensor has changed
        if (bottomSensorRead != bottomSensorState) {
          bottomSensorState = bottomSensorRead; // Update the previous state
          sensorChanged = true;  // Mark that a sensor change occurred
          publishMqtt(true, bottomSensorState ? "on" : "off");  // Publish the state change via MQTT
          DEBUG_PRINTLN(F("Bottom sensor changed."));
        }

        // Check if the state of the top sensor has changed
        if (topSensorRead != topSensorState) {
          topSensorState = topSensorRead; // Update the previous state
          sensorChanged = true;  // Mark that a sensor change occurred
          publishMqtt(false, topSensorState ? "on" : "off");  // Publish the state change via MQTT
          DEBUG_PRINTLN(F("Top sensor changed."));
        }

        // Check if the state of the enable switch has changed
        if (enableSwitchRead != enableSwitchState) {
          enableSwitchState = enableSwitchRead; // Update the previous state
          DEBUG_PRINTLN(F("EnableSwitch changed."));
        }

        // Reset the flags for API calls
        topSensorWrite = false;
        bottomSensorWrite = false;
        enableSwitchWrite = false;

        // If any sensor state has changed, update the light state
        if (sensorChanged) {
          lastSwitchTime = millis();  // Update the last switch time
          if (topSensorState || bottomSensorState) {
            // Determine the direction based on the last sensor activated
            lastSensor = topSensorRead;
          }

          // Toggle power on if necessary and all segments are off
          if (!on && togglePower && (topIndex == maxSegmentId || bottomIndex == (minSegmentId-1)) && offMode) toggleOnOff(); 
          if (enableSwitchState == false) return false;  // Disable if the switch is off

          DEBUG_PRINT(F("ON -> lastSensor "));
          DEBUG_PRINTLN(lastSensor ? F("up.") : F("down."));

          // Reset the indices for the correct direction of the swipe
          if (topSensorRead && topIndex == maxSegmentId) {
            topIndex = minSegmentId;
          }
          if (bottomSensorRead && bottomIndex == (minSegmentId-1)) {
            bottomIndex = maxSegmentId-1;
          }
          on = true;  // Turn on the lights
        }
      }
      return sensorChanged;  // Return whether any sensor state changed
    }

    // Function to automatically power off the lights after the set time
    void autoPowerOff() {
      if ((millis() - lastSwitchTime) > on_time_ms || enableSwitchState == false) {
        // If sensors are still on, do nothing
        if (enableSwitchState == true && (bottomSensorState || topSensorState)) return;

        // Turn off the lights in the direction of the last sensor activation
        swipe = lastSensor;
        if (lastSensor) {
          disableIndex = minSegmentId;
        } else {
          disableIndex = maxSegmentId - 1;
        }
        on = false;  // Turn off the lights

        DEBUG_PRINT(F("OFF -> lastSensor "));
        DEBUG_PRINTLN(lastSensor ? F("up.") : F("down."));
      }
    }

    // Function to update the swipe effect on the staircase
    void updateSwipe() {
      if ((millis() - lastTime) > segment_delay_ms) {
        lastTime = millis();  // Update the last action time

        updateSegments();  // Update the light segments based on the swipe direction
        // Toggle power off if necessary and all segments are off
        if (togglePower && (topIndex == maxSegmentId || bottomIndex == (minSegmentId-1)) && !offMode && !on) toggleOnOff();  
      }
    }

    // Function to send sensor values to the JSON API
    void writeSensorsToJson(JsonObject& staircase) {
      staircase[F("top-sensor")]    = topSensorRead;  // Current state of the top sensor
      staircase[F("bottom-sensor")] = bottomSensorRead;  // Current state of the bottom sensor
      staircase[F("enable-switch")] = enableSwitchRead;  // Current state of the enable switch
      staircase[F("on")] = on;  // Whether the staircase lights are on
      staircase[F("topIndex")] = topIndex;  // Current top segment index
      staircase[F("bottomIndex")] = bottomIndex;  // Current bottom segment index
      staircase[F("disableIndex")] = disableIndex;  // Current disable segment index
    }

    // Function to allow overriding sensor values via the JSON API
    void readSensorsFromJson(JsonObject& staircase) {
      bottomSensorWrite = bottomSensorState || (staircase[F("bottom-sensor")].as<bool>());  // Override bottom sensor state
      topSensorWrite    = topSensorState    || (staircase[F("top-sensor")].as<bool>());  // Override top sensor state
      enableSwitchWrite = enableSwitchState || (staircase[F("enable-switch")].as<bool>());  // Override enable switch state
    }

    // Function to enable or disable the usermod
    void enable(bool enable) {
      if (enable) {
        DEBUG_PRINTLN(F("Animated Staircase enabled."));
        DEBUG_PRINT(F("Delay between steps: "));
        DEBUG_PRINT(segment_delay_ms);
        DEBUG_PRINT(F(" milliseconds.\nStairs switch off after: "));
        DEBUG_PRINT(on_time_ms / 1000);
        DEBUG_PRINTLN(F(" seconds."));

        // Configure pins for sensors and switches
        pinMode(bottomPIRorTriggerPin, INPUT);
        pinMode(topPIRorTriggerPin, INPUT);
        pinMode(enableSwitchPin, INPUT);

        // Set the segment IDs for the staircase
        minSegmentId = strip.getMainSegmentId(); 
        maxSegmentId = strip.getLastActiveSegmentId() + 1;
        topIndex = maxSegmentId;
        bottomIndex = disableIndex = minSegmentId - 1;

        // Adjust the strip transition time
        transitionDelay = segment_delay_ms;
        strip.setTransition(segment_delay_ms);
        strip.trigger();

        on = true;  // Turn on the lights
      } else {
        // Toggle power on if necessary when disabling
        if (togglePower && !on && offMode) toggleOnOff(); 

        // Restore segment options and force update the strip
        for (int i = 0; i <= strip.getLastActiveSegmentId(); i++) {
          Segment &seg = strip.getSegment(i);
          if (!seg.isActive()) continue; 
          seg.setOption(SEG_OPTION_ON, true);
        }
        strip.trigger();  
        stateChanged = true;  
        colorUpdated(CALL_MODE_DIRECT_CHANGE);
        DEBUG_PRINTLN(F("Animated Staircase disabled."));
      }
      enabled = enable;  // Update the enabled state
    }

  public:
    // Setup function to initialize the usermod
    void setup() {
      // Standardize invalid pin numbers to -1
      if (topPIRorTriggerPin    < 0) topPIRorTriggerPin    = -1;
      if (bottomPIRorTriggerPin < 0) bottomPIRorTriggerPin = -1;
      if (enableSwitchPin < 0) enableSwitchPin = -1;

      // Allocate pins for sensors and switches
      PinManagerPinType pins[3] = {
        { topPIRorTriggerPin, false },
        { bottomPIRorTriggerPin, false },
        { enableSwitchPin, false },
      };
      // Allocate pins and disable usermod if allocation fails
      if (!pinManager.allocateMultiplePins(pins, 3, PinOwner::UM_AnimatedStaircase)) {
        topPIRorTriggerPin = -1;
        bottomPIRorTriggerPin = -1;
        enableSwitchPin = -1;
        enabled = false;
      }
      enable(enabled);  // Enable the usermod based on the stored state
      initDone = true;  // Mark initialization as complete
    }

    // Main loop function to handle the usermod logic
    void loop() {
      if (!enabled || strip.isUpdating()) return;  // Exit if the usermod is disabled or the strip is updating
      minSegmentId = strip.getMainSegmentId();  
      maxSegmentId = strip.getLastActiveSegmentId() + 1;
      checkSensors();  // Check the sensors for state changes
      if (on) autoPowerOff();  // Automatically power off the lights if necessary
      updateSwipe();  // Update the swipe effect
    }

    // Function to return the unique ID of the usermod
    uint16_t getId() { return USERMOD_ID_ANIMATED_STAIRCASE; }

#ifndef WLED_DISABLE_MQTT
    /**
     * Handle incoming MQTT messages
     * Topic contains stripped topic (part after /wled/MAC)
     * Topic should look like: /swipe with a message of [up|down]
     */
    bool onMqttMessage(char* topic, char* payload) {
      if (strlen(topic) == 6 && strncmp_P(topic, PSTR("/swipe"), 6) == 0) {
        String action = payload;
        if (action == "up") {
          bottomSensorWrite = true;  // Simulate a bottom sensor activation
          return true;
        } else if (action == "down") {
          topSensorWrite = true;  // Simulate a top sensor activation
          return true;
        } else if (action == "on") {
          enable(true);  // Enable the usermod
          return true;
        } else if (action == "off") {
          enable(false);  // Disable the usermod
          return true;
        }
      }
      return false;
    }

    /**
     * Subscribe to MQTT topic for controlling the usermod
     */
    void onMqttConnect(bool sessionPresent) {
      // Subscribe to the relevant MQTT topics
      char subuf[64];
      if (mqttDeviceTopic[0] != 0) {
        strcpy(subuf, mqttDeviceTopic);
        strcat_P(subuf, PSTR("/swipe"));
        mqtt->subscribe(subuf, 0);
      }
    }
#endif

    // Function to add the current state to JSON (for API integration)
    void addToJsonState(JsonObject& root) {
      JsonObject staircase = root[FPSTR(_name)];
      if (staircase.isNull()) {
        staircase = root.createNestedObject(FPSTR(_name));  // Create a nested JSON object if it doesn't exist
      }
      writeSensorsToJson(staircase);  // Write the current sensor states to the JSON object
      DEBUG_PRINTLN(F("Staircase sensor state exposed in API."));
    }

    /*
    * Reads configuration settings from the JSON API.
    * See void addToJsonState(JsonObject& root)
    */
    void readFromJsonState(JsonObject& root) {
      if (!initDone) return;  // Prevent crash on boot applyPreset()
      bool en = enabled;
      JsonObject staircase = root[FPSTR(_name)];
      if (!staircase.isNull()) {
        if (staircase[FPSTR(_enabled)].is<bool>()) {
          en = staircase[FPSTR(_enabled)].as<bool>();  // Read the enabled state from JSON
        } else {
          String str = staircase[FPSTR(_enabled)];  // Checkbox -> off or on
          en = (bool)(str!="off"); // Convert to boolean (off is guaranteed to be present)
        }
        if (en != enabled) enable(en);  // Enable or disable based on JSON input
        readSensorsFromJson(staircase);  // Read sensor states from JSON
        DEBUG_PRINTLN(F("Staircase sensor state read from API."));
      }
    }

    // Function to append configuration data (placeholder function)
    void appendConfigData() {
      //oappend(SET_F("dd=addDropdown('staircase','selectfield');"));
      //oappend(SET_F("addOption(dd,'1st value',0);"));
      //oappend(SET_F("addOption(dd,'2nd value',1);"));
      //oappend(SET_F("addInfo('staircase:selectfield',1,'additional info');"));  // 0 is field type, 1 is actual field
    }

    /*
    * Writes the configuration to internal flash memory.
    */
    void addToConfig(JsonObject& root) {
      JsonObject staircase = root[FPSTR(_name)];
      if (staircase.isNull()) {
        staircase = root.createNestedObject(FPSTR(_name));  // Create a nested JSON object if it doesn't exist
      }
      staircase[FPSTR(_enabled)]                   = enabled;  // Save the enabled state
      staircase[FPSTR(_segmentDelay)]              = segment_delay_ms;  // Save the segment delay
      staircase[FPSTR(_onTime)]                    = on_time_ms / 1000;  // Save the on-time (in seconds)
      staircase[FPSTR(_topPIRorTrigger_pin)]       = topPIRorTriggerPin;  // Save the top sensor pin
      staircase[FPSTR(_bottomPIRorTrigger_pin)]    = bottomPIRorTriggerPin;  // Save the bottom sensor pin
      staircase[FPSTR(_enableSwitch_pin)]          = enableSwitchPin;  // Save the enable switch pin
      staircase[FPSTR(_togglePower)]               = togglePower;  // Save the toggle power option
      DEBUG_PRINTLN(F("Staircase config saved."));
    }

    /*
    * Reads the configuration from internal flash memory before setup() is called.
    * 
    * The function should return true if configuration was successfully loaded or false if there was no configuration.
    */
    bool readFromConfig(JsonObject& root) {
      int8_t oldTopAPin = topPIRorTriggerPin;
      int8_t oldBottomAPin = bottomPIRorTriggerPin;
      int8_t oldEnableSwitchPin = enableSwitchPin;

      JsonObject top = root[FPSTR(_name)];
      if (top.isNull()) {
        DEBUG_PRINT(FPSTR(_name));
        DEBUG_PRINTLN(F(": No config found. (Using defaults.)"));
        return false;
      }

      // Load configuration values from the JSON object
      enabled   = top[FPSTR(_enabled)] | enabled;
      segment_delay_ms = top[FPSTR(_segmentDelay)] | segment_delay_ms;
      segment_delay_ms = (unsigned long) min((unsigned long)10000,max((unsigned long)10,(unsigned long)segment_delay_ms));  // max delay 10s

      on_time_ms = top[FPSTR(_onTime)] | on_time_ms/1000;
      on_time_ms = min(900,max(1,(int)on_time_ms)) * 1000; // min 1s, max 15min

      topPIRorTriggerPin = top[FPSTR(_topPIRorTrigger_pin)] | topPIRorTriggerPin;
      bottomPIRorTriggerPin = top[FPSTR(_bottomPIRorTrigger_pin)] | bottomPIRorTriggerPin;
      enableSwitchPin = top[FPSTR(_enableSwitch_pin)] | enableSwitchPin;
      togglePower = top[FPSTR(_togglePower)] | togglePower;  // staircase toggles power on/off

      DEBUG_PRINT(FPSTR(_name));
      if (!initDone) {
        // First run: reading from cfg.json
        DEBUG_PRINTLN(F(" config loaded."));
      } else {
        // Changing parameters from settings page
        DEBUG_PRINTLN(F(" config (re)loaded."));
        bool changed = false;
        if ((oldTopAPin != topPIRorTriggerPin) ||
            (oldBottomAPin != bottomPIRorTriggerPin) ||
            (oldEnableSwitchPin != enableSwitchPin)) {
          changed = true;
          pinManager.deallocatePin(oldTopAPin, PinOwner::UM_AnimatedStaircase);
          pinManager.deallocatePin(oldBottomAPin, PinOwner::UM_AnimatedStaircase);
          pinManager.deallocatePin(oldEnableSwitchPin, PinOwner::UM_AnimatedStaircase);
        }
        if (changed) setup();  // Re-setup if pins have changed
      }
      return !top[FPSTR(_togglePower)].isNull();  // Return true if toggle power is configured
    }

    /*
    * Shows the delay between steps and power-off time in the "info"
    * tab of the web-UI.
    */
    void addToJsonInfo(JsonObject& root) {
      JsonObject user = root["u"];
      if (user.isNull()) {
        user = root.createNestedObject("u");  // Create a nested JSON object if it doesn't exist
      }

      JsonArray infoArr = user.createNestedArray(FPSTR(_name));  // Create a JSON array for the staircase info

      String uiDomString = F("<button class=\"btn btn-xs\" onclick=\"requestJson({");
      uiDomString += FPSTR(_name);
      uiDomString += F(":{");
      uiDomString += FPSTR(_enabled);
      uiDomString += enabled ? F(":false}});\">") : F(":true}});\">");
      uiDomString += F("<i class=\"icons ");
      uiDomString += enabled ? "on" : "off";
      uiDomString += F("\">&#xe08f;</i></button>");
      infoArr.add(uiDomString);  // Add the UI button for toggling the staircase
    }
};

// Strings to reduce flash memory usage (used more than twice)
const char Animated_Staircase::_name[]                      PROGMEM = "staircase";
const char Animated_Staircase::_enabled[]                   PROGMEM = "enabled";
const char Animated_Staircase::_segmentDelay[]              PROGMEM = "segment-delay-ms";
const char Animated_Staircase::_onTime[]                    PROGMEM = "on-time-s";
const char Animated_Staircase::_topPIRorTrigger_pin[]       PROGMEM = "topPIRorTrigger_pin";
const char Animated_Staircase::_bottomPIRorTrigger_pin[]    PROGMEM = "bottomPIRorTrigger_pin";
const char Animated_Staircase::_enableSwitch_pin[]          PROGMEM = "enableSwitch_pin";
const char Animated_Staircase::_togglePower[]               PROGMEM = "toggle-on-off";
