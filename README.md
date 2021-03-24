# alarm-keypad-mqtt
A program written for arduino ethernet, having a keypad and two light diodes connected. It is sending and receiving MQTT messages and is used to control an alarm. An example how to integrate with Home Assistant is provided. You can save up to nine different alarm codes in the EEPROM.

![Keypad setup](https://ekblad.org/ha/keypad.jpg)

Keypad commands:

* "\*" - Turn on alarm mode, result: send ALARM_MODE_ON (if delayExit > 0 wait delayExit seconds before sending ALARM_MODE_ON, Press '\*' again to cancel before time is out)
* "CODE\*" - Turn off alarm and alarm mode, (CODE is 1-9 digits), sends: CODE_FAIL,TURN_ON_ALARM (third time wrong code is entered) or (ALARM_MODE_OFF and ALARM_OFF)
* "#CODE\*P\*NEW_CODE\*" - Store NEW_CODE (1-9 digits) in register P (P is 1-MAX_NUMBER_OF_CODES), send: CODE_CHANGED, OPERATION_FAILED, CODE_FAIL, TURN_ON_ALARM (third time wrong code is entered)

If delayEntry > 0 and the MQTT message "ENTER" is recieved in the MQTT_TOPIC_IN topic and we are armed, Send ALARM_MODE_OFF and ALARM_OFF and wait for delayEntry seconds. If a correct code is not entered during that time, send ALARM_MODE_ON, ALARM_ON.

If no code is stored in any register, the default CODE is 12345678

The Keypad communicates with a MQTT server, it subscribes to topic MQTT_TOPIC_IN and send data to topic MQTT_TOPIC_OUT. The Keypad recieves some MQTT-messages from the server, ENTER (for delayed entry), ALARM_MODE_ON, ALARM_MODE_OFF, ALARM_ON, ALARM_OFF (and update the Keypad status accordingly)

There is a "watchdog" running (default every minute) that's checking the connection to the MQTT server. If the connection is down, the Ethernet connection (default using DHCP) and connection to the MQTT server is trying to be set up again. 

The Keypad assumes a red light diode is connected to A0 (Analog pin 0) and a green light diode is connected to A1 (Each light diode also have a 220 ohm resistor and is connected to ground)

I'm using a 12 keys keypad, connect keypad pin 1-7 to arduino digital pin 9-3

Lights:

* a solid green light - Connected to Ethernet network and MQTT server. The default is to use DHCP to retrieve an IP-number
* a blinking green light - Not connected to Ethernet or MQTT server
* no red light - Not armed, no alarm
* a solid red light - Armed, no alarm
* a more slowly blinking red light - Alarm
* a fast blinking red light - "Delay exit" (waiting before arming and sending ALARM_MODE_ON) or "Delay entry" (turn off alarm and wating for enter the correct code)
* A red light turns on and off once - various error conditions, for example wrong code entered

Example how to integrate with Home Assistant, enter the following in `config/configuration.yaml`:
```
switch:
  - platform: mqtt
    name: "Alarm"
    state_topic: "keypad_out"
    command_topic: "keypad_in"
    qos: 0
    payload_on: "ALARM_ON"
    payload_off: "ALARM_OFF"
    retain: true
    
  - platform: mqtt
    name: "Arming"
    state_topic: "keypad_out"
    command_topic: "keypad_in"
    qos: 0
    payload_on: "ALARM_MODE_ON"
    payload_off: "ALARM_MODE_OFF"
    retain: true
```

If you have a magnet sensor on the front door, you might want to send a MQTT message to the Keypad to enter "delayed entry" mode, in this case just add the following in the `config/automations.yaml`:
```
- alias: Trigger delay entry mode for alarm
  description: ''
  trigger:
  - type: opened
    platform: device
    device_id: my_sensor_magnet_id
    entity_id: my_sensor_magnet_on_off
    domain: binary_sensor
  condition: []
  action:
  - service: mqtt.publish
    data:
      topic: keypad_in
      payload: ENTER
  mode: single
```

Here is another example how to starting to playing an ambulance sound on the squeezebox when the alarm is triggered and stop when alarm is turned off, add this in `config/automations.yaml`:
```
- alias: Start Alarm
  description: ''
  trigger:
  - platform: mqtt
    topic: keypad_out
    payload: ALARM_ON
  condition: []
  action:
  - service: squeezebox.call_method
    data:
      entity_id: media_player.squeezebox_classic
      command: playlist
      parameters:
      - play
      - loop://content.mysqueezebox.com/static/sounds/effects/ambulance.mp3
    entity_id: media_player.squeezebox_classic
  mode: single

- alias: Stop Alarm
  description: ''
  trigger:
  - platform: mqtt
    topic: keypad_out
    payload: ALARM_OFF
  condition: []
  action:
  - service: squeezebox.call_method
    data:
      entity_id: media_player.squeezebox_classic
      command: stop
    entity_id: media_player.squeezebox_classic
  mode: single
```
And here is an example how to get a bunch of of motion sensors to trigger the alarm, add this in the `config/automations.yaml`:
```
- id: '1616621337300'
  alias: Trigger alarm
  description: ''
  trigger:
  - platform: state
    entity_id: binary_sensor.ikea_of_sweden_tradfri_motion_sensor_01234567_on_off
    to: 'on'
  - platform: state
    entity_id: binary_sensor.ikea_of_sweden_tradfri_motion_sensor_01234568_on_off
    to: 'on'
  - platform: state
    entity_id: binary_sensor.ikea_of_sweden_tradfri_motion_sensor_01234569_on_off
    to: 'on'
  - platform: state
    entity_id: binary_sensor.ikea_of_sweden_tradfri_motion_sensor_0123456a_on_off
    to: 'on'
  condition:
  - condition: state
    entity_id: switch.arming
    state: 'on'
  action:
  - service: mqtt.publish
    data:
      topic: keypad_in
      payload: ALARM_ON
  mode: single

```
