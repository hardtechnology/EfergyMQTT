# EfergyMQTT
Efergy MQTT is a simple Efergy Data protocol to MQTT IoT code base designed to run on the ESP8266

The Efergy Power meters run at 433MHz (in AU) with FSK protocol. Using the DATA_OUT pad inside the Efergy E2 Classic or Elite Classic Receiver this code will allow you to capture this data and send it to an MQTT server of your choice. It will also output information on the Serial port of the ESP8266 for diagnostics.


# What's Supported
* Configuration of MQTT Server including TCP Port, username, password
* MQTT Will Topic for Online status of EfergyMQTT
* Configuration of WiFi Settings
* Reporting of Transmitter Battery Status (retained message)
* Notification of Transmitters that have their 'Link' button pressed recently
* Transmitter Online status (retained message)
* Transmitter lost packet
* Power usage in both mA (millAmps) and Watts


# Programming the ESP8266 Module
This code is creating in the Arudino IDE version 1.6.9. You will need to seperately install the required libraries and your ESP8266 board configuration.

Required Libraries (add them through the Arduino Libaries Manager)
* WiFiManager
* ArduinoJson
* PubsubClient


# Configuration
Once programmed, it will boot up in AP mode. This may take 1-2 minutes upon first boot as the file system is being prepared. You can then connect to the 'EfergyMQTT' access point with passphrase 'TTQMygrefE'. Once connected, browse to a random HTTP website and you will be redirected to the configuration page. Fille out your MQTT server information and the voltage of your power supply and click Save. The ESP will then restart and connect to your configured WiFi and MQTT Server.

To change the configuration the ESP needs to boot without being able to connect to your configured Access Point. Turning off the AP, or disabling the MAC address of the ESP in your AP will allow access to the configuration next time the ESP restarts.


# Hooking it up
This is designed to run on an Adafruit Huzzah or Wemos D1 mini which will fit inside the battery compartment of the Efergy Receiver unit. Due to the additional power draw of the ESP module, the Efergy receiver will need a 5V power supply. This can be tapped into along with the DATA out of the RF receivier in the unit to operate the ESP8266 module.The DATA Out from the receiver should be hooked up to pin 16 on the ESP8266 module.
