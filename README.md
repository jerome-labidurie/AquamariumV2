# Aquamarium V2

An *IoT* device to show current tide level.

Revival of an old pject from **Fablab Lannion**.
See :
* [Aquamarium](https://wiki.fablab-lannion.org/index.php?title=AquaMarium) for the original project page
* [github](https://github.com/FablabLannion/DataPlus/tree/master/AquaMarium) for the original code & hardware

## Hardware
* Weemos D1 mini
* WS2812 strips
* button for reset

## Software
* Software/Aquamarium/ for the esp8266 firmware

Data are gathered from a mqtt server as json object.
A simulation script is provided for testing & dev.

Usage :
```bash
# install mqtt server
$ apt install mosquitto mosquitto-clients
# start simulation tool
$ ./fake_data.sh -h
usage: ./fake_data.sh [-h] [-d <delay in sec>] [-l <number of loops>] -i file.txt
$ ./fake_data.sh -d 1 -l 10 -i full_me.txt 
# check that data is comming
$ mosquitto_sub -h localhost -t tides/trebeurden/json
{"location":"trebeurden","high":{"timestamp":"2018-10-15 23:04 +0200","level":7.57,"coefficient":48},"current":{"timestamp":"16/10/18 04:30 +0200","level":3.78,"clock":152},"low":{"timestamp":"2018-10-16 05:28 +0200","level":3.52,"coefficient":48}} 
```


