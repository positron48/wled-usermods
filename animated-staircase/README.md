# Animated Staircase Usermod

## Overview

The Animated Staircase usermod is designed to control the lighting of a staircase based on motion detection. The lights turn on when a person is detected at the top or bottom of the staircase and gradually turn off when the person leaves the staircase. The usermod supports configuration via the web interface and saves the settings in the device's memory.

## Configuration

Below are the main configuration parameters:

- **`enabled`**: Enables or disables the usermod (`true` or `false`). When disabled, the lighting will not respond to the sensors.
- **`segment_delay_ms`**: The delay (in milliseconds) between switching each segment of the staircase lighting.
- **`on_time_ms`**: The time (in milliseconds) the lights remain on after motion is detected.
- **`topPIRorTriggerPin`**: The pin number connected to the motion sensor at the top of the staircase. 
- **`bottomPIRorTriggerPin`**: The pin number connected to the motion sensor at the bottom of the staircase.
- **`enableSwitchPin`**: The pin number for a hardware switch that enables or disables the usermod (can be used for a light sensor).

## Operation Logic

### Main Process

1. **Motion Detection**: The sensors at the top and bottom of the staircase are checked for motion at regular intervals (`scanDelay`).
   
2. **Turning On the Lights**: 
   - When motion is detected by either sensor, the corresponding index (`topIndex` or `bottomIndex`) is adjusted to start turning on the staircase segments in sequence.
   - The lights turn on in the direction of movement (either upwards or downwards) until all segments are illuminated.
   
3. **Automatic Power-Off**:
   - The lights remain on for the duration specified by `on_time_ms`.
   - If no further motion is detected within this period, the lights will start turning off in the direction opposite to the last detected movement.
   
4. **Sensor and Switch State Handling**:
   - The state of the sensors and the enable switch is continuously monitored and updated.
   - Changes in sensor states are published via MQTT (if enabled) to inform other devices or systems.


## Code Structure

### Main Components

- **Functions**:
  - `setup()`: Initializes the usermod, setting up pins and enabling the usermod if configured.
  - `loop()`: The main loop function that handles sensor checking, segment updates, and automatic power-off.
  - `updateSegments()`: Updates the segments of the staircase lighting based on the current state.
  - `checkSensors()`: Checks the state of the sensors and processes any changes.
  - `autoPowerOff()`: Automatically turns off the lights after the configured time.
  - `publishMqtt()`: Publishes sensor states to an MQTT topic.
  - `readFromJsonState()` and `addToJsonState()`: Handle reading and writing the usermod's state through the JSON API.
  - `addToConfig()` and `readFromConfig()`: Save and load the usermod's configuration to and from the device's memory.
