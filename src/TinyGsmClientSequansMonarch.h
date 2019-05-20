/**
 * @file       TinyGsmClientSequansMonarch.h
 * @author     Michael Krumpus
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2019 Michael Krumpus
 * @date       Jan 2019
 */

#ifndef TinyGsmClientSequansMonarch_h
#define TinyGsmClientSequansMonarch_h

//#define TINY_GSM_DEBUG Serial
//#define TINY_GSM_USE_HEX

#if !defined(TINY_GSM_RX_BUFFER)
  #define TINY_GSM_RX_BUFFER 64
#endif

#define TINY_GSM_MUX_COUNT 5

#include <TinyGsmCommon.h>

#define GSM_NL "\r"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

enum SimStatus {
  SIM_ERROR = 0,
  SIM_READY = 1,
  SIM_LOCKED = 2,
};

enum RegStatus {
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};

#define NUM_SOCKETS 6

enum SocketStatus {
  SOCK_CLOSED                 = 0,
  SOCK_ACTIVE_DATA            = 1,
  SOCK_SUSPENDED              = 2,
  SOCK_SUSPENDED_PENDING_DATA = 3,
  SOCK_LISTENING              = 4,
  SOCK_INCOMING               = 5,
  SOCK_OPENING                = 6,
};


class TinyGsmSequansMonarch
{

public:

class GsmClient : public Client
{
  friend class TinyGsmSequansMonarch;
  typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

public:
  GsmClient() {}

  GsmClient(TinyGsmSequansMonarch& modem, uint8_t mux = 1) {
    init(&modem, mux);
  }

  bool init(TinyGsmSequansMonarch* modem, uint8_t mux = 1) {
    this->at = modem;
    this->mux = mux;
    sock_available = 0;
    prev_check = 0;
    sock_connected = false;
    got_data = false;

    at->sockets[mux] = this;

    return true;
  }

public:
  virtual int connect(const char *host, uint16_t port, int timeout_s) {
    if (sock_connected) stop();
    TINY_GSM_YIELD();
    rx.clear();
    sock_connected = at->modemConnect(host, port, mux, false, timeout_s);
    return sock_connected;
  }

TINY_GSM_CLIENT_CONNECT_OVERLOADS()

  virtual void stop() {
    TINY_GSM_YIELD();
    // Read and dump anything remaining in the modem's internal buffer.
    // The socket will appear open in response to connected() even after it
    // closes until all data is read from the buffer.
    // Doing it this way allows the external mcu to find and get all of the data
    // that it wants from the socket even if it was closed externally.
    rx.clear();
    at->maintain();
    while (sock_available > 0) {
      at->modemRead(TinyGsmMin((uint16_t)rx.free(), sock_available), mux);
      rx.clear();
      at->maintain();
    }
    at->sendAT(GF("+SQNSH="), mux);
    sock_connected = false;
    at->waitResponse();
  }

TINY_GSM_CLIENT_WRITE()

TINY_GSM_CLIENT_AVAILABLE_WITH_BUFFER_CHECK()

TINY_GSM_CLIENT_READ_WITH_BUFFER_CHECK()

TINY_GSM_CLIENT_PEEK_FLUSH_CONNECTED()

  /*
   * Extended API
   */

  String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;

private:
  TinyGsmSequansMonarch* at;
  uint8_t         mux;
  uint16_t        sock_available;
  uint32_t        prev_check;
  bool            sock_connected;
  bool            got_data;
  RxFifo          rx;
};


class GsmClientSecure : public GsmClient
{
public:
  GsmClientSecure() {}

  GsmClientSecure(TinyGsmSequansMonarch& modem, uint8_t mux = 1)
    : GsmClient(modem, mux)
  {}

protected:
  bool          strictSSL = false;

public:
  virtual int connect(const char *host, uint16_t port, int timeout_s) {
    stop();
    TINY_GSM_YIELD();
    rx.clear();

    // configure security profile 1 with parameters:
    if (strictSSL) {
      // require minimum of TLS 1.2 (3)
      // only support cipher suite 0x3D: TLS_RSA_WITH_AES_256_CBC_SHA256
      // verify server certificate against imported CA certs 0 and enforce validity period (3)
      at->sendAT(GF("+SQNSPCFG=1,3,\"0x3D\",3,0,,,\"\",\"\""));
    } else {
      // use TLS 1.0 or higher (1)
      // support wider variety of cipher suites
      // do not verify server certificate (0)
      at->sendAT(GF("+SQNSPCFG=1,1,\"0x2F;0x35;0x3C;0x3D\",0,,,,\"\",\"\""));
    }
    if (at->waitResponse() != 1) {
      DBG("failed to configure security profile");
      return false;
    }

    sock_connected = at->modemConnect(host, port, mux, true, timeout_s);
    return sock_connected;
  }

  void setStrictSSL(bool strict) {
    strictSSL = strict;
  }

};

public:

  TinyGsmSequansMonarch(Stream& stream)
    : stream(stream)
  {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */

  bool begin(const char* pin = NULL) {
    return init(pin);
  }

  bool init(const char* pin = NULL) {
    DBG(GF("### TinyGSM Version:"), TINYGSM_VERSION);
    if (!testAT()) {
      return false;
    }
    sendAT(GF("E0"));   // Echo Off
    if (waitResponse() != 1) {
      return false;
    }
    getSimStatus();
    return true;
  }

  String getModemName() {
    return "Sequans Monarch";
  }

TINY_GSM_MODEM_SET_BAUD_IPR()

  bool testAT(unsigned long timeout_ms = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout_ms; ) {
      sendAT(GF(""));
      if (waitResponse(200) == 1) {
          delay(100);
          return true;
      }
      delay(100);
    }
    return false;
  }

TINY_GSM_MODEM_MAINTAIN_CHECK_SOCKS()

  bool factoryDefault() {
    sendAT(GF("&FZE0&W"));  // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("+IPR=0"));   // Auto-baud
    waitResponse();
    sendAT(GF("+IFC=0,0")); // No Flow Control
    waitResponse();
    sendAT(GF("+ICF=3,3")); // 8 data 0 parity 1 stop
    waitResponse();
    sendAT(GF("+CSCLK=0")); // Disable Slow Clock
    waitResponse();
    sendAT(GF("&W"));       // Write configuration
    return waitResponse() == 1;
  }

TINY_GSM_MODEM_GET_INFO_ATI()

  bool hasSSL() {
    return true;
  }

  /*
   * Power functions
   */

  bool restart() {
    if (!testAT()) {
      return false;
    }

    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }

    sendAT(GF("+CFUN=1,1"));
    if (waitResponse(60000L, GF("+SYSSTART")) != 1) {
      return false;
    }
    delay(1000);
    return init();
  }

  bool poweroff() {
    sendAT(GF("+SQNSSHDN"));
    return waitResponse();
  }

  bool radioOff() {
    sendAT(GF("+CFUN=0"));
    if (waitResponse(10000L) != 1) {
      return false;
    }
    delay(3000);
    return true;
  }

  /*
    During sleep, the SIM800 module has its serial communication disabled. In order to reestablish communication
    pull the DRT-pin of the SIM800 module LOW for at least 50ms. Then use this function to disable sleep mode.
    The DTR-pin can then be released again.
  */
  bool sleepEnable(bool enable = true) {
    sendAT(GF("+CSCLK="), enable);
    return waitResponse() == 1;
  }

  /*
   * SIM card functions
   */

TINY_GSM_MODEM_SIM_UNLOCK_CPIN()

  String getSimCCID() {
    sendAT(GF("+SQNCCID"));
    if (waitResponse(GF(GSM_NL "+SQNCCID:")) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

TINY_GSM_MODEM_GET_IMEI_GSN()

  SimStatus getSimStatus(unsigned long timeout_ms = 10000L) {
    for (unsigned long start = millis(); millis() - start < timeout_ms; ) {
      sendAT(GF("+CPIN?"));
      if (waitResponse(GF(GSM_NL "+CPIN:")) != 1) {
        delay(1000);
        continue;
      }
      int status = waitResponse(GF("READY"), GF("SIM PIN"), GF("SIM PUK"), GF("NOT INSERTED"));
      waitResponse();
      switch (status) {
      case 2:
      case 3:  return SIM_LOCKED;
      case 1:  return SIM_READY;
      default: return SIM_ERROR;
      }
    }
    return SIM_ERROR;
  }

TINY_GSM_MODEM_GET_REGISTRATION_XREG(CEREG)

TINY_GSM_MODEM_GET_OPERATOR_COPS()

  /*
   * Generic network functions
   */

TINY_GSP_MODEM_GET_CSQ()

  bool isNetworkConnected() {
    RegStatus s = getRegistrationStatus();
    if (s == REG_OK_HOME || s == REG_OK_ROAMING) {
      DBG(F("connected with status:"), s);
      return true;
    } else {
      return false;
    }
  }

TINY_GSM_MODEM_WAIT_FOR_NETWORK()

  /*
   * GPRS functions
   */

  bool gprsConnect(const char* apn, const char* user = NULL, const char* pwd = NULL) {
    gprsDisconnect();

    // Define the PDP context
    sendAT(GF("+CGDCONT=3,\"IPV4V6\",\""), apn, '"');
    waitResponse();

    if (user && strlen(user) > 0) {
      sendAT(GF("+CGAUTH=3,1,\""), user, GF("\",\""), pwd, GF("\""));
      waitResponse();
    }

    // Activate the PDP context
    sendAT(GF("+CGACT=1,3"));
    waitResponse(60000L);

    // Attach to GPRS
    sendAT(GF("+CGATT=1"));
    if (waitResponse(60000L) != 1)
      return false;

    return true;
  }

  bool gprsDisconnect() {
    sendAT(GF("+CGATT=0"));
    if (waitResponse(60000L) != 1)
      return false;

    return true;
  }

  bool isGprsConnected() {
    sendAT(GF("+CGATT?"));
    if (waitResponse(GF(GSM_NL "+CGATT:")) != 1) {
      return false;
    }
    int res = stream.readStringUntil('\n').toInt();
    waitResponse();
    if (res != 1)
      return false;

    return true;
  }


  /*
   * IP Address functions
   */

  String getLocalIP() {
    sendAT(GF("+CGPADDR=3"));

    if (waitResponse(10000L, GF("+CGPADDR: 3,\"")) != 1) {
      return "";
    }
    String res = stream.readStringUntil('\"');
    waitResponse();
    return res;
  }

  IPAddress localIP() {
    return TinyGsmIpFromString(getLocalIP());
  }


  /*
   * Phone Call functions
   */

  bool setGsmBusy(bool busy = true) TINY_GSM_ATTR_NOT_AVAILABLE;

  bool callAnswer() TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool callNumber(const String& number) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool callHangup() TINY_GSM_ATTR_NOT_IMPLEMENTED;

  String sendUSSD(const String& code) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  bool sendSMS(const String& number, const String& text) {
    sendAT(GF("+CMGF=1"));
    waitResponse();
    //Set GSM 7 bit default alphabet (3GPP TS 23.038)
    sendAT(GF("+CSCS=\"GSM\""));
    waitResponse();
    sendAT(GF("+CMGS=\""), number, GF("\""));
    if (waitResponse(GF(">")) != 1) {
      return false;
    }
    stream.print(text);
    stream.write((char)0x1A);
    stream.flush();
    return waitResponse(60000L) == 1;
  }

  bool sendSMS_UTF16(const String& number, const void* text, size_t len) TINY_GSM_ATTR_NOT_IMPLEMENTED;

  /*
   * Location functions
   */

  String getGsmLocation() TINY_GSM_ATTR_NOT_AVAILABLE;


  /*
   * Battery functions
   */

  uint16_t getBattVoltage() TINY_GSM_ATTR_NOT_AVAILABLE;

  int getBattPercent() TINY_GSM_ATTR_NOT_AVAILABLE;

protected:

  bool modemConnect(const char* host, uint16_t port, uint8_t mux,
                    bool ssl = false, int timeout_s = 75)
 {
    int rsp;
    unsigned long startMillis = millis();
    uint32_t timeout_ms = timeout_s*1000;

    if (ssl) {
      // enable SSl and use security profile 1
      sendAT(GF("+SQNSSCFG="), mux, GF(",1,1"));
      if (waitResponse() != 1) {
        DBG("failed to configure secure socket");
        return false;
      }
    }

    sendAT(GF("+SQNSCFG="), mux, GF(",3,300,90,600,50"));
    waitResponse();

    sendAT(GF("+SQNSCFGEXT="), mux, GF(",1,0,0,0,0"));
    waitResponse();

    sendAT(GF("+SQNSD="), mux, ",0,", port, ',', GF("\""), host, GF("\""), ",0,0,1");
    rsp = waitResponse((timeout_ms - (millis() - startMillis)),
                      GF("OK" GSM_NL),
                      GF("NO CARRIER" GSM_NL)
                      );

    // creation of socket failed immediately.
    if (rsp != 1) return rsp;

    // wait until we get a good status
    bool connected = false;
    while (!connected && ((millis() - startMillis) < timeout_ms)) {
      connected = modemGetConnected(mux);
      delay(100); // socket may be in opening state
    }
    return connected;
  }


  int modemSend(const void* buff, size_t len, uint8_t mux) {
    sendAT(GF("+SQNSSENDEXT="), mux, ',', len);
    if (waitResponse(5000, GF(GSM_NL "> ")) != 1) {
      return 0;
    }
    stream.write((uint8_t*)buff, len);
    stream.flush();
    if (waitResponse() != 1) {
      DBG("no OK after send");
      return 0;
    }
    return len;
  }


  size_t modemRead(size_t size, uint8_t mux) {
    sendAT(GF("+SQNSRECV="), mux, ',', size);
    if (waitResponse(GF("+SQNSRECV: ")) != 1) {
      return 0;
    }
    streamSkipUntil(','); // Skip mux
    size_t len = stream.readStringUntil('\n').toInt();
    for (size_t i=0; i<len; i++) {
      while (!stream.available()) { TINY_GSM_YIELD(); }
      char c = stream.read();
      sockets[mux]->rx.put(c);
    }
    waitResponse();
    sockets[mux]->sock_available = modemGetAvailable(mux);
    return len;
  }

  size_t modemGetAvailable(uint8_t mux) {
    sendAT(GF("+SQNSI="), mux);
    size_t result = 0;
    if (waitResponse(GF("+SQNSI:")) == 1) {
      streamSkipUntil(','); // Skip mux
      streamSkipUntil(','); // Skip sent
      streamSkipUntil(','); // Skip received
      result = stream.readStringUntil(',').toInt();
      waitResponse();
    }
    if (!result) {
      sockets[mux]->sock_connected = modemGetConnected(mux);
    }
    return result;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+SQNSS"));
    uint8_t m = 0;
    uint8_t status = 0;

    while (true) {
      if (waitResponse(GFP(GSM_OK), GF(GSM_NL "+SQNSS: ")) != 2) {
        break;
      };
      m = stream.readStringUntil(',').toInt();
      if (m == mux) {
        status = stream.readStringUntil(',').toInt();
      }
      streamSkipUntil('\n'); // Skip
    }

    return ((status != SOCK_CLOSED) && (status != SOCK_INCOMING) && (status != SOCK_OPENING));
  }

public:

  /*
   Utilities
   */

TINY_GSP_MODEM_STREAM_UTILITIES()

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout_ms, String& data,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        int a = stream.read();
        if (a <= 0) continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+SQNSRING:"))) {
          int mux = stream.readStringUntil(',').toInt();
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->got_data = true;
          }
          stream.readStringUntil('\n');
          data = "";
        } else if (data.endsWith(GF("SQNSH: "))) {
          int mux = stream.readStringUntil('\n').toInt();
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->sock_connected = false;
          }
          data = "";
          DBG("### Closed: ", mux);
        }
      }
    } while (millis() - startMillis < timeout_ms);
finish:
    if (!index) {
      data.trim();
      if (data.length()) {
        DBG("### Unhandled:", data);
      }
      data = "";
    }
    return index;
  }

  uint8_t waitResponse(uint32_t timeout_ms,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String data;
    return waitResponse(timeout_ms, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

public:
  Stream&       stream;

protected:
  GsmClient*    sockets[TINY_GSM_MUX_COUNT];
};

#endif