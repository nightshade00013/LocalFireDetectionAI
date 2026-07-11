#ifndef HA_COMM_H
#define HA_COMM_H

#include <Arduino.h>

void ha_init();
void ha_loop();
void ha_publish_detection(bool is_fire, float prob);
void ha_publish_sensors(int mq2, int mq5, int mq7, float temp, float hum, bool flame);

#endif // HA_COMM_H
