#include <Arduino.h>
#define TINY_GSM_MODEM_SIM7070
#define TINY_GSM_RX_BUFFER 1024
#include <TinyGsmClient.h>
#include "modem.h"

#define MODEM_TX    27
#define MODEM_RX    26
#define MODEM_PWR   4
#define MODEM_BAUD  115200

#define SerialAT    Serial1
TinyGsm modem(SerialAT);

static void modemPowerOn()
{
	pinMode(MODEM_PWR, OUTPUT);
	digitalWrite(MODEM_PWR, HIGH);
	delay(100); 
	digitalWrite(MODEM_PWR, LOW);
	delay(1000); 
	digitalWrite(MODEM_PWR, HIGH);
	delay(2000); 
}

bool modemInit()
{
	modemPowerOn();
	SerialAT.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
	
	uint32_t start = millis();
	while (millis() - start < 10000) {
		SerialAT.println("AT");
		delay(500);
		if (SerialAT.available()) {
			if (SerialAT.readString().indexOf("OK") >= 0) goto found;
		}
	}
	return false;

found:
	if (!modem.init()) return false;
	modem.sendAT("+CTZU=1");
	modem.waitResponse();
	return modem.waitForNetwork(30000L);
}

bool modemGetTimestamp(struct tm &out)
{
	while(SerialAT.available()) SerialAT.read();
	modem.sendAT("+CCLK?");
	if (modem.waitResponse(2000, "+CCLK: ") != 1) return false;
	String res = SerialAT.readStringUntil('\n');
	res.replace("\"", "");
	res.trim();
	if (res.length() < 17) return false;
	
	int yy = res.substring(0, 2).toInt();
	int mm = res.substring(3, 5).toInt();
	int dd = res.substring(6, 8).toInt();
	int hh = res.substring(9, 11).toInt();
	int min = res.substring(12, 14).toInt();
	int sec = res.substring(15, 17).toInt();
	
	if (2000 + yy < 2024) return false;
	
	memset(&out, 0, sizeof(struct tm));
	out.tm_year = (2000 + yy) - 1900;
	out.tm_mon = mm - 1;
	out.tm_mday = dd;
	out.tm_hour = hh;
	out.tm_min = min;
	out.tm_sec = sec;
	out.tm_isdst = -1;
	return true;
}