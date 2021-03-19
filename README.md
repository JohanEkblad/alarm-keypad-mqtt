# alarm-keypad-mqtt
A program written for arduino ethernet, having a keypad and two light diodes connected. It is sending and receiving MQTT messages and is used to control an alarm. An example how to integrate with Home Assistant is provided. You can save up to nine different alarm codes in the EEPROM.

Keypad commands:

* "\*" - Turn on alarm mode, result: send ALARM_MODE_ON (if delayExit > 0 wait delayExit seconds before sending ALARM_MODE_ON, Press '\*' again to cancel before time is out)
* "CODE\*" - Turn off alarm and alarm mode, (CODE is 1-9 digits), sends: CODE_FAIL,TURN_ON_ALARM (third time wrong code is entered) or (ALARM_MODE_OFF and ALARM_OFF)
* "#CODE\*P\*NEW_CODE\*" - Store NEW_CODE (1-9 digits) in register P (P is 1-MAX_NUMBER_OF_CODES), send: CODE_CHANGED, OPERATION_FAILED, CODE_FAIL, TURN_ON_ALARM (third time wrong code is entered)

If delayEntry > 0 and the MQTT message MQTT_ENTER_PAYLOAD is recieved in the MQTT_TOPIC_IN topic and we are armed, Send ALARM_MODE_OFF and ALARM_OFF and wait for delayEntry seconds. If a correct code is not entered during that time, send ALARM_MODE_ON, ALARM_ON.

If no code is stored in any register, the default CODE is 12345678

The Keypad communicates with a MQTT server, it subscribes to topic MQTT_TOPIC_IN and send data to topic MQTT_TOPIC_OUT

The Keypad assumes a red light diode is connected to A0 (Analog pin 0) and a green light diode is connected to A1 (Each light diode also have a 200 ohm resistor and is connected to ground)

Lights:

* a solid green light - Connected to Ethernet network and MQTT server. The default is to use DHCP to retrieve an IP-number
* a blinking green light - Not connected to Ethernet or MQTT server
* no red light - Not armed, no alarm
* a solid red light - Armed, no alarm
* a more slowly blinking red light - Alarm
* a fast blinking red light - "Delay exit" (waiting before arming and sending ALARM_MODE_ON) or "Delay entry" (turn off alarm and wating for enter the correct code)
* A red light turns on and off once - various error conditions, for example wrong code entered
