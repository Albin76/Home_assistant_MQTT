# Home_assistant_MQTT
Sensors and control via MQTT

This rep is focused on the HA/MQTT part. 


struct __attribute__((packed)) SENSOR_DATA {\
        int     checkstart;\
        int     sendno;\
	int	sensor;\
	char	MQTT_sensor_topic[15];\
	int	millis;\
	float	temp;\
	float	humidity;\
	float	pressure;\
	float	battery;\
	float	spare1;\
	float	spare2;\
	int	checkend;\
} sensorData;		

ESP32 is not the latest. Continued improvements in Visual code studio in separate rep.
