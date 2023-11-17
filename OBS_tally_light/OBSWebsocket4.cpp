#include <Arduino.h>
#include <WebSocketsClient.h>

#include <ArduinoJson.h>

class OBSSocket4 {
protected:
  IPAddress _ip;
  uint16_t _port;
  WebSocketsClient _webSocket;

  bool _connected = false;
  bool _onAir = false;
  String _currentProgramScene = "";
  String _currentPreviewScene = "";
  boolean _authRequired = false;
  boolean _checkAuth = true;

public:
  OBSSocket4() {}

  void begin(const IPAddress ip) {
    begin(ip, 4444);
  }
  void begin(const IPAddress ip, const uint16_t port) {
    _ip = ip;
    _port = port;

    _connected = false;
  }

  void connect() {
    _webSocket.disconnect();
    // server address, port and URL
    _webSocket.begin(_ip, _port, "/");
    // event handler
    // _webSocket.onEvent(webSocketEvent);
    _webSocket.onEvent([&](WStype_t type, uint8_t* payload, size_t length) {
      this->webSocketEvent(type, payload, length);
    });
    // try every 5000ms again if connection has failed
    _webSocket.setReconnectInterval(5000);

    // start heartbeat (optional)
    // ping server every 15000 ms
    // expect pong from server within 3000 ms
    // consider connection disconnected if pong is not received 2 times
    _webSocket.enableHeartbeat(15000, 3000, 2);
    _sendInitialMessages();

    delay(5000);
  }

  void _sendInitialMessages() {
    _webSocket.sendTXT("{\"request-type\":\"GetAuthRequired\",\"message-id\":\"1\"}");
    _webSocket.sendTXT("{\"request-type\":\"GetCurrentScene\",\"message-id\":\"2\"}");
    _webSocket.sendTXT("{\"request-type\":\"GetPreviewScene\",\"message-id\":\"3\"}");
    _webSocket.sendTXT("{\"request-type\":\"SetHeartbeat\",\"enable\":true,\"message-id\":\"4\"}");
  }

  void runLoop() {
    _webSocket.loop();

    if (_connected) {
      if (!_authRequired) {
        if (_checkAuth) {
          _sendInitialMessages();
        }
      } else {
        _authRequired = false;
        _checkAuth = true;
      }
    }
  }

  bool isConnected() {
    return _connected;
  }
  bool isRejected() {
    return false;  // TODO
  }

  bool getStreamStreaming() {
    return _onAir;
  }
  String getProgramScene() {
    return _currentProgramScene;
  }
  String getPreviewScene() {
    return _currentPreviewScene;
  }


  void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_DISCONNECTED:
        _connected = false;
        break;

      case WStype_CONNECTED:
        _connected = true;
        break;

      case WStype_TEXT:
        {
          DynamicJsonDocument resDoc(4069);
          // Deserialize the JSON document
          DeserializationError error = deserializeJson(resDoc, payload);

          // Test if parsing succeeds.
          if (error) {
            return;
          }

          String messageId = resDoc["message-id"];
          if (messageId == "1") {
            _checkAuth = false;
            _authRequired = resDoc["authRequired"];

            /*
            USE_SERIAL.printf("DEBUG 1");
            SHA256 hasher;
            byte shaResult[SHA256_SIZE];
            
            USE_SERIAL.printf("DEBUG 2");
              
            String salt = resDoc["salt"];
            String challenge = resDoc["challenge"];
            
            USE_SERIAL.printf("DEBUG 3");
            
            String secretString = OBS_WEBSOCKET_PASS + salt;
            int strLength = secretString.length();
            char* secretCharArr;
            secretString.toCharArray(secretCharArr, strLength);

            USE_SERIAL.printf("DEBUG 4");
            
            hasher.doUpdate(secretCharArr);
            hasher.doFinal(shaResult);
            
            USE_SERIAL.printf("DEBUG 5");
            
            String result = (char*) shaResult;
            String secret = base64::encode(result + challenge);
            strLength = secret.length();
            char* secretArr;
            secret.toCharArray(secretArr, strLength);
            hasher.doUpdate(secretArr);
            hasher.doFinal(shaResult);
            result = (char*) shaResult;
            String authResponse = base64::encode(result);

            StaticJsonDocument<512> responseDoc;
            responseDoc["request-type"] = "Authenticate";
            responseDoc["message-id"] = messageIdCounter++;
            responseDoc["auth"] = authResponse;
            String responseBody;        
            webSocket.sendTXT(responseBody); */
          } else if (messageId == "2") {
            _currentProgramScene = (String)resDoc["name"];

          } else if (messageId == "3") {
            _currentPreviewScene = (String)resDoc["name"];

          } else {
            String updateType = (String)resDoc["update-type"];
            if (updateType == "SwitchScenes") {
              _currentProgramScene = (String)resDoc["scene-name"];
            }

            if (updateType == "PreviewSceneChanged") {
              _currentPreviewScene = (String)resDoc["scene-name"];
            }

            if (updateType == "StreamStatus" || updateType == "Heartbeat") {
              _onAir = resDoc["streaming"];
            }

            if (updateType == "StreamStopped") {
              _onAir = false;
            }

            if (updateType == "Heartbeat") {
              _currentProgramScene = (String)resDoc["current-scene"];
            }
          }
          break;
        }

      case WStype_ERROR:
        break;

      case WStype_PING:
        // pong will be send automatically
        break;

      case WStype_PONG:
        // answer to a ping we send
        break;
    }
  }
};
