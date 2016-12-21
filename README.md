# EfergyMQTT
Efergy MQTT is a simple Efergy Data protocol to MQTT IoT code base designed to run on the ESP8266

The Efergy Power meters run a 433MHz (in AU) FSK protocol. Using the DATA_OUT pad inside the Efergy E2 Classic or Elite Classic Receiver this code will allow you to capture this data and send it to an MQTT server of your choice

# What's Supported
* Reporting of Transmitter Battery Status
* Notification of Transmitters that have their 'Link' button pressed recently
* Transmitter Online status* Transmitter lost packet
* Power usage in both mA (millAmps) and Watts

# Programming the ESP8266 Module
TBC

# Configuration
TBC

# Hooking it up
This is designed to run on an Adafruit Huzzah or Wemos D1 mini which will fit inside the battery compartment of the Efergy Receiver unit. Due to the additional power draw of the ESP module, the Efergy receiver will need a 5V power supply. This can be tapped into along with the DATA out of the RF receivier in the unit to operate the ESP8266 module.The DATA Out from the receiver should be hooked up to pin 16 on the ESP8266 module.
