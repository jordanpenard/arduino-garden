# arduino-garden

The plan is to automate watering of the plants, and gather some metrics while we're at it. The initial setup is going to be one soil moisture sensor and one pump to water the plants.
Everything runs off the Arduino for now, but a future improvment can be to have a dedicated server dealing with data storage and web interface and have the Arduino do get the measurments and send them to the server.

Storage of the data, log and web pages is currently in the flash of the esp8266. On boot the esp8266 pulls from the branch `web-live` in github the latest web pages.

WiFi setup is handled through a hotspot on first boot or if the currently setup wifi can't be connected to. 

## TODO
- Data query should allow for either a number of points or data after a given timestamp
- Add a menu to the webpages
- Think about storage full
- Graph should have the option to see data for the last day, week, month, year, all
- File storage initialisation
- Add support for extra sensors (air temperature, air humidity, air pressure, ...)
