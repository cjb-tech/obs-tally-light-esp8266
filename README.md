# OBS_tally_light

Tally light для OBS. Тобто світлячок, який кріпиться на камеру і показує, чи камера в ефірі чи ні.

Основу прошивки стащив з репозиторія [ATEM_tally_light_with_ESP8266 від @AronHetLam](https://github.com/AronHetLam/ATEM_tally_light_with_ESP8266). Код для роботи з OBS - з репозиторія [obs-esp8266-tallylight від @jowedel](https://github.com/jowedel/obs-esp8266-tallylight).

Протокол - OBS Websocket v4. На відміну від оригінальної прошивки, пристрої не працюють один для одного як проксі. 

Використані матеріали:

- [Wemos D1 mini на базі ESP8266](https://beegreen.com.ua/plata-wemos-d1-mini-wifi-na-baze-esp8266-arduino-avr-12894)
- [Корпус пластиковий DIP](https://erg.com.ua/ua/p210915255-korpus-plastikovyj-dip.html)
- Триколірний світлодіод (з спільним *катодом*)
- Резистори десь на 150 Ом

Інструкція по збірці оригінального пристрою - [wiki](https://github.com/AronHetLam/ATEM_tally_light_with_ESP8266/wiki/DIY-guide) або [YouTube](https://www.youtube.com/watch?v=238HlUx3dgI).
