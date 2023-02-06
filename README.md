# Wemos D1 mini를 사용한 UART(RS485) 시리얼 모니터링 웹서버

RS485 네트워크의 시리얼 데이터를 무선(WiFi)으로 확인하기위한 ESP8266 프로젝트

## 기능

1. AP모드에서 사용할 와이파이 네트워크를 선택하고 저장할 수 있음
2. 클라이언트에서 웹으로 접속한 후 시리얼 데이터를 웹소켓을 통해 주고받을 수 있음
3. 프리셋 패킷저장 및 재활용 가능

config.ini 파일에 "ssid;password" 형식으로 기본 접속할 Wifi 정보가 저장됨
config.ini 파일이 있는경우 wifi에 접속을 시도함
기본 SSDI로 접속이 실패하는경우 AP모드로 동작함
AP모드에서는 주변 Wifi를 검색해서 공개된 SSID를 표시함
기본 접속 SSID를 선택해서 접속하면 config.ini파일에 정보를 저장함

preset.ini 파일에 웹소켓으로 전송할 패킷 프리셋을 저장할 수 있음

## 사용 라이브러리

- ESP8266WiFi
- AsyncFSBrowser
- ESPAsyncTCP

## Hardware (준비중)
