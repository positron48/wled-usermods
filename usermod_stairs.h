/*
 * Usermod for detecting people entering/leaving a staircase and switching the
 * staircase on/off.
 *
 * Edit the Animated_Staircase_config.h file to compile this usermod for your
 * specific configuration.
 * 
 * See the accompanying README.md file for more info.
 */
#pragma once
#include "wled.h"

class Animated_Staircase : public Usermod {
  private:

    /* configuration (available in API and stored in flash) */
    bool enabled = false;                   // Enable this usermod
    unsigned long segment_delay_ms = 150;   // Time between switching each segment
    unsigned long on_time_ms       = 10000; // The time for the light to stay on
    int8_t topPIRorTriggerPin      = -1;    // disabled
    int8_t bottomPIRorTriggerPin   = -1;    // disabled
    int8_t enableSwitchPin         = -1;    // hardware switch to disable usermod (light sensor e.g.)
    bool togglePower               = false; // toggle power on/off with staircase on/off

    /* runtime variables */
    bool initDone = false;

    // Time between checking of the sensors
    const unsigned int scanDelay = 100;

    // Lights on or off.
    // Flipping this will start a transition.
    bool on = false;

    // Indicates which Sensor was seen last (to determine
    // the direction when swiping off)
  #define LOWER false
  #define UPPER true
    bool lastSensor = LOWER;
    bool swipe = LOWER;

    // Time of the last transition action
    unsigned long lastTime = 0;

    // Time of the last sensor check
    unsigned long lastScanTime = 0;

    // Last time the lights were switched on or off
    unsigned long lastSwitchTime = 0;

    // segment id between onIndex and offIndex are on.
    // controll the swipe by setting/moving these indices around.
    // onIndex must be less than or equal to offIndex
    int8_t topIndex = 0;
    int8_t bottomIndex = 0;
    int8_t disableIndex = 0;

    // The maximum number of configured segments.
    // Dynamically updated based on user configuration.
    byte maxSegmentId = 1;
    byte minSegmentId = 0;

    // These values are used by the API to read the
    // last sensor state, or trigger a sensor
    // through the API
    bool topSensorRead     = false;
    bool topSensorWrite    = false;
    bool bottomSensorRead  = false;
    bool bottomSensorWrite = false;
    bool enableSwitchRead  = false;
    bool enableSwitchWrite = false;
    bool topSensorState    = false;
    bool bottomSensorState = false;
    bool enableSwitchState = false;

    // strings to reduce flash memory usage (used more than twice)
    static const char _name[];
    static const char _enabled[];
    static const char _segmentDelay[];
    static const char _onTime[];
    static const char _topPIRorTrigger_pin[];
    static const char _bottomPIRorTrigger_pin[];
    static const char _enableSwitch_pin[];
    static const char _togglePower[];

    void publishMqtt(bool bottom, const char* state) {
#ifndef WLED_DISABLE_MQTT
      //Check if MQTT Connected, otherwise it will crash the 8266
      if (WLED_MQTT_CONNECTED){
        char subuf[64];
        sprintf_P(subuf, PSTR("%s/motion/%d"), mqttDeviceTopic, (int)bottom);
        mqtt->publish(subuf, 0, false, state);
      }
#endif
    }

    void updateSegments() {
      // Check and update segments from the top end
      if (topIndex < maxSegmentId && topIndex >= minSegmentId) {
          strip.getSegment(topIndex).setOption(SEG_OPTION_ON, true);
          topIndex++;
      }

      // Check and update segments from the bottom end
      if (bottomIndex >= minSegmentId && bottomIndex < maxSegmentId) {
          strip.getSegment(bottomIndex).setOption(SEG_OPTION_ON, true);
          bottomIndex--;
      }
      
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
          
      strip.trigger();  // force strip refresh
      stateChanged = true;  // inform external devices/UI of change
      colorUpdated(CALL_MODE_DIRECT_CHANGE);
    }

    bool checkSensors() {
      bool sensorChanged = false;

      if ((millis() - lastScanTime) > scanDelay) {
        lastScanTime = millis();

        bottomSensorRead = bottomSensorWrite || (bottomPIRorTriggerPin<0 ? false : digitalRead(bottomPIRorTriggerPin));
        topSensorRead = topSensorWrite || (topPIRorTriggerPin<0 ? false : digitalRead(topPIRorTriggerPin));
        enableSwitchRead = enableSwitchWrite || (enableSwitchPin<0 ? false : digitalRead(enableSwitchPin));

        if (bottomSensorRead != bottomSensorState) {
          bottomSensorState = bottomSensorRead; // change previous state
          sensorChanged = true;
          publishMqtt(true, bottomSensorState ? "on" : "off");
          DEBUG_PRINTLN(F("Bottom sensor changed."));
        }

        if (topSensorRead != topSensorState) {
          topSensorState = topSensorRead; // change previous state
          sensorChanged = true;
          publishMqtt(false, topSensorState ? "on" : "off");
          DEBUG_PRINTLN(F("Top sensor changed."));
        }

        if (enableSwitchRead != enableSwitchState) {
          enableSwitchState = enableSwitchRead; // change previous state
          DEBUG_PRINTLN(F("EnableSwitch changed."));
        }

        // Values read, reset the flags for next API call
        topSensorWrite = false;
        bottomSensorWrite = false;
        enableSwitchWrite = false;

        if (sensorChanged) {
          lastSwitchTime = millis();
          if (topSensorState || bottomSensorState) {
            // If the bottom sensor triggered, we need to swipe up, ON
            lastSensor = topSensorRead;
          }

          if (!on && togglePower && (topIndex == maxSegmentId || bottomIndex == (minSegmentId-1)) && offMode) toggleOnOff(); // toggle power on if off
          if (enableSwitchState == false) return false; 

          DEBUG_PRINT(F("ON -> lastSensor "));
          DEBUG_PRINTLN(lastSensor ? F("up.") : F("down."));

          // Position the indices for a correct on-swipe
          if (topSensorRead && topIndex == maxSegmentId) {
            topIndex = minSegmentId;
          }
          if (bottomSensorRead && bottomIndex == (minSegmentId-1)) {
            bottomIndex = maxSegmentId-1;
          }
          on = true;
        }
      }
      return sensorChanged;
    }

    void autoPowerOff() {
      if ((millis() - lastSwitchTime) > on_time_ms || enableSwitchState == false) {
        // if sensors are still on, do nothing
        if (enableSwitchState == true && (bottomSensorState || topSensorState)) return;

        // OFF in the direction of the last sensor detection
        swipe = lastSensor;
        if (lastSensor) {
          disableIndex = minSegmentId;
        } else {
          disableIndex = maxSegmentId - 1;
        }
        on = false;

        DEBUG_PRINT(F("OFF -> lastSensor "));
        DEBUG_PRINTLN(lastSensor ? F("up.") : F("down."));
      }
    }

    void updateSwipe() {
      if ((millis() - lastTime) > segment_delay_ms) {
        lastTime = millis();

        updateSegments(); // reduce the number of updates to necessary ones
        if (togglePower && (topIndex == maxSegmentId || bottomIndex == (minSegmentId-1)) && !offMode && !on) toggleOnOff();  // toggle power off for all segments off
      }
    }

    // send sensor values to JSON API
    void writeSensorsToJson(JsonObject& staircase) {
      staircase[F("top-sensor")]    = topSensorRead;
      staircase[F("bottom-sensor")] = bottomSensorRead;
      staircase[F("enable-switch")] = enableSwitchRead;
      staircase[F("on")] = on;
      staircase[F("topIndex")] = topIndex;
      staircase[F("bottomIndex")] = bottomIndex;
      staircase[F("disableIndex")] = disableIndex;
    }

    // allow overrides from JSON API
    void readSensorsFromJson(JsonObject& staircase) {
      bottomSensorWrite = bottomSensorState || (staircase[F("bottom-sensor")].as<bool>());
      topSensorWrite    = topSensorState    || (staircase[F("top-sensor")].as<bool>());
      enableSwitchWrite = enableSwitchState || (staircase[F("enable-switch")].as<bool>());
    }

    void enable(bool enable) {
      if (enable) {
        DEBUG_PRINTLN(F("Animated Staircase enabled."));
        DEBUG_PRINT(F("Delay between steps: "));
        DEBUG_PRINT(segment_delay_ms);
        DEBUG_PRINT(F(" milliseconds.\nStairs switch off after: "));
        DEBUG_PRINT(on_time_ms / 1000);
        DEBUG_PRINTLN(F(" seconds."));

        pinMode(bottomPIRorTriggerPin, INPUT);
        pinMode(topPIRorTriggerPin, INPUT);
        pinMode(enableSwitchPin, INPUT);

        minSegmentId = strip.getMainSegmentId(); // it may not be the best idea to start with main segment as it may not be the first one
        maxSegmentId = strip.getLastActiveSegmentId() + 1;
        topIndex = maxSegmentId;
        bottomIndex = disableIndex = minSegmentId - 1;

        // shorten the strip transition time to be equal or shorter than segment delay
        transitionDelay = segment_delay_ms;
        strip.setTransition(segment_delay_ms);
        strip.trigger();

        on = true;
      } else {
        if (togglePower && !on && offMode) toggleOnOff(); // toggle power on if off
        // Restore segment options
        for (int i = 0; i <= strip.getLastActiveSegmentId(); i++) {
          Segment &seg = strip.getSegment(i);
          if (!seg.isActive()) continue; // skip vector gaps
          seg.setOption(SEG_OPTION_ON, true);
        }
        strip.trigger();  // force strip update
        stateChanged = true;  // inform external devices/UI of change
        colorUpdated(CALL_MODE_DIRECT_CHANGE);
        DEBUG_PRINTLN(F("Animated Staircase disabled."));
      }
      enabled = enable;
    }

  public:
    void setup() {
      // standardize invalid pin numbers to -1
      if (topPIRorTriggerPin    < 0) topPIRorTriggerPin    = -1;
      if (bottomPIRorTriggerPin < 0) bottomPIRorTriggerPin = -1;
      if (enableSwitchPin < 0) enableSwitchPin = -1;
      // allocate pins
      PinManagerPinType pins[3] = {
        { topPIRorTriggerPin, false },
        { bottomPIRorTriggerPin, false },
        { enableSwitchPin, false },
      };
      // NOTE: this *WILL* return TRUE if all the pins are set to -1.
      //       this is *BY DESIGN*.
      if (!pinManager.allocateMultiplePins(pins, 3, PinOwner::UM_AnimatedStaircase)) {
        topPIRorTriggerPin = -1;
        bottomPIRorTriggerPin = -1;
        enableSwitchPin = -1;
        enabled = false;
      }
      enable(enabled);
      initDone = true;
    }

    void loop() {
      if (!enabled || strip.isUpdating()) return;
      minSegmentId = strip.getMainSegmentId();  // it may not be the best idea to start with main segment as it may not be the first one
      maxSegmentId = strip.getLastActiveSegmentId() + 1;
      checkSensors();
      if (on) autoPowerOff();
      updateSwipe();
    }

    uint16_t getId() { return USERMOD_ID_ANIMATED_STAIRCASE; }

#ifndef WLED_DISABLE_MQTT
    /**
     * handling of MQTT message
     * topic only contains stripped topic (part after /wled/MAC)
     * topic should look like: /swipe with amessage of [up|down]
     */
    bool onMqttMessage(char* topic, char* payload) {
      if (strlen(topic) == 6 && strncmp_P(topic, PSTR("/swipe"), 6) == 0) {
        String action = payload;
        if (action == "up") {
          bottomSensorWrite = true;
          return true;
        } else if (action == "down") {
          topSensorWrite = true;
          return true;
        } else if (action == "on") {
          enable(true);
          return true;
        } else if (action == "off") {
          enable(false);
          return true;
        }
      }
      return false;
    }

    /**
     * subscribe to MQTT topic for controlling usermod
     */
    void onMqttConnect(bool sessionPresent) {
      //(re)subscribe to required topics
      char subuf[64];
      if (mqttDeviceTopic[0] != 0) {
        strcpy(subuf, mqttDeviceTopic);
        strcat_P(subuf, PSTR("/swipe"));
        mqtt->subscribe(subuf, 0);
      }
    }
#endif

    void addToJsonState(JsonObject& root) {
      JsonObject staircase = root[FPSTR(_name)];
      if (staircase.isNull()) {
        staircase = root.createNestedObject(FPSTR(_name));
      }
      writeSensorsToJson(staircase);
      DEBUG_PRINTLN(F("Staircase sensor state exposed in API."));
    }

    /*
    * Reads configuration settings from the json API.
    * See void addToJsonState(JsonObject& root)
    */
    void readFromJsonState(JsonObject& root) {
      if (!initDone) return;  // prevent crash on boot applyPreset()
      bool en = enabled;
      JsonObject staircase = root[FPSTR(_name)];
      if (!staircase.isNull()) {
        if (staircase[FPSTR(_enabled)].is<bool>()) {
          en = staircase[FPSTR(_enabled)].as<bool>();
        } else {
          String str = staircase[FPSTR(_enabled)]; // checkbox -> off or on
          en = (bool)(str!="off"); // off is guaranteed to be present
        }
        if (en != enabled) enable(en);
        readSensorsFromJson(staircase);
        DEBUG_PRINTLN(F("Staircase sensor state read from API."));
      }
    }

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
        staircase = root.createNestedObject(FPSTR(_name));
      }
      staircase[FPSTR(_enabled)]                   = enabled;
      staircase[FPSTR(_segmentDelay)]              = segment_delay_ms;
      staircase[FPSTR(_onTime)]                    = on_time_ms / 1000;
      staircase[FPSTR(_topPIRorTrigger_pin)]       = topPIRorTriggerPin;
      staircase[FPSTR(_bottomPIRorTrigger_pin)]    = bottomPIRorTriggerPin;
      staircase[FPSTR(_enableSwitch_pin)]          = enableSwitchPin;
      staircase[FPSTR(_togglePower)]               = togglePower;
      DEBUG_PRINTLN(F("Staircase config saved."));
    }

    /*
    * Reads the configuration to internal flash memory before setup() is called.
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
        // first run: reading from cfg.json
        DEBUG_PRINTLN(F(" config loaded."));
      } else {
        // changing parameters from settings page
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
        if (changed) setup();
      }
      // use "return !top["newestParameter"].isNull();" when updating Usermod with new features
      return !top[FPSTR(_togglePower)].isNull();
    }

    /*
    * Shows the delay between steps and power-off time in the "info"
    * tab of the web-UI.
    */
    void addToJsonInfo(JsonObject& root) {
      JsonObject user = root["u"];
      if (user.isNull()) {
        user = root.createNestedObject("u");
      }

      JsonArray infoArr = user.createNestedArray(FPSTR(_name));  // name

      String uiDomString = F("<button class=\"btn btn-xs\" onclick=\"requestJson({");
      uiDomString += FPSTR(_name);
      uiDomString += F(":{");
      uiDomString += FPSTR(_enabled);
      uiDomString += enabled ? F(":false}});\">") : F(":true}});\">");
      uiDomString += F("<i class=\"icons ");
      uiDomString += enabled ? "on" : "off";
      uiDomString += F("\">&#xe08f;</i></button>");
      infoArr.add(uiDomString);
    }
};

// strings to reduce flash memory usage (used more than twice)
const char Animated_Staircase::_name[]                      PROGMEM = "staircase";
const char Animated_Staircase::_enabled[]                   PROGMEM = "enabled";
const char Animated_Staircase::_segmentDelay[]              PROGMEM = "segment-delay-ms";
const char Animated_Staircase::_onTime[]                    PROGMEM = "on-time-s";
const char Animated_Staircase::_topPIRorTrigger_pin[]       PROGMEM = "topPIRorTrigger_pin";
const char Animated_Staircase::_bottomPIRorTrigger_pin[]    PROGMEM = "bottomPIRorTrigger_pin";
const char Animated_Staircase::_enableSwitch_pin[]          PROGMEM = "enableSwitch_pin";
const char Animated_Staircase::_togglePower[]               PROGMEM = "toggle-on-off";
