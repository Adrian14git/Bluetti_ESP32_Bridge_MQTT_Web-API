#ifndef MQTT_H
#define MQTT_H
#include "Arduino.h"
#include "DeviceType.h"

extern void publishTopic(enum field_names field_name, String value);
extern void publishHAConfig();
extern void publishDeviceState();
extern void publishDeviceStateStatus();
extern void publishBluetoothQueryDelayState();
extern String getMqttMeasurementsJson();
extern String map_field_name(enum field_names f_name);
extern String map_command_value(String command_name, String value);
extern void deviceServoPress(int degree);
extern void handleMQTT();
extern void initMQTT();
extern bool isMQTTEnabled();
extern void setMQTTEnabled(bool enabled);
extern bool isMQTTconnected();
extern int getPublishErrorCount();
unsigned long getLastMQTTMessageTime();
unsigned long getLastMQTTDeviceStateMessageTime();
unsigned long getLastMQTTDeviceStateStatusMessageTime();

#endif
