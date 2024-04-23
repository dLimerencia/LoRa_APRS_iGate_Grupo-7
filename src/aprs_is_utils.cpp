#include <WiFi.h>
#include "configuration.h"
#include "aprs_is_utils.h"
#include "station_utils.h"
#include "syslog_utils.h"
#include "query_utils.h"
#include "digi_utils.h"
#include "display.h"
#include "utils.h"

extern Configuration        Config;
extern WiFiClient           espClient;
extern uint32_t             lastScreenOn;
extern String               firstLine;
extern String               secondLine;
extern String               thirdLine;
extern String               fourthLine;
extern String               fifthLine;
extern String               sixthLine;
extern String               seventhLine;

extern std::vector<String>  outputPacketBuffer;
extern uint32_t             lastRxTime;


namespace APRS_IS_Utils {

    void upload(String line) {
        espClient.print(line + "\r\n");
    }

    void connect() {
        uint8_t count = 0;
        String aprsauth;
        Serial.print("Connecting to APRS-IS ...     ");
        while (!espClient.connect(Config.aprs_is.server.c_str(), Config.aprs_is.port) && count < 20) {
            Serial.println("Didn't connect with server...");
            delay(1000);
            espClient.stop();
            espClient.flush();
            Serial.println("Run client.stop");
            Serial.println("Trying to connect with Server: " + String(Config.aprs_is.server) + " AprsServerPort: " + String(Config.aprs_is.port));
            count++;
            Serial.println("Try: " + String(count));
        }
        if (count == 20) {
            Serial.println("Tried: " + String(count) + " FAILED!");
        }
        else {
            Serial.println("Connected!\n(Server: " + String(Config.aprs_is.server) + " / Port: " + String(Config.aprs_is.port) + ")");

            // String filter = "t/m/" + Config.callsign + "/" + (String)Config.aprs_is.reportingDistance;

            aprsauth = "user " + Config.callsign + " pass " + Config.aprs_is.passcode + " vers CA2RXU_LoRa_iGate 1.3 filter " + Config.aprs_is.filter;
            upload(aprsauth);
            delay(200);
        }
    }

    void checkStatus() {
        String wifiState, aprsisState;
        if (WiFi.status() == WL_CONNECTED) {
            wifiState = "OK";
        }
        else {
            wifiState = "AP";

            if (!Config.display.alwaysOn) {
                display_toggle(true);
            }

            if (Config.display.alwaysOn) {
                lastScreenOn = millis();
            }
        }

        if (!Config.aprs_is.active) {
            aprsisState = "OFF";
        }
        else if (espClient.connected()) {
            aprsisState = "OK";
        }
        else {
            aprsisState = "--";

            if (!Config.display.alwaysOn) {
                display_toggle(true);
            }

            if (Config.display.alwaysOn) {
                lastScreenOn = millis();
            }
        }

        secondLine = "WiFi: " + wifiState + " APRS-IS: " + aprsisState;
    }

    String buildPacketToUpload(String packet) {
        if (!(Config.aprs_is.active && Config.digi.mode == 0)) { // Check if NOT only IGate
            return packet.substring(3, packet.indexOf(":")) + ",qAR," + Config.callsign + packet.substring(packet.indexOf(":"));
        }
        else {
            return packet.substring(3, packet.indexOf(":")) + ",qAO," + Config.callsign + packet.substring(packet.indexOf(":"));
        }
    }

    String buildPacketToTx(String aprsisPacket) {
        String firstPart, messagePart;
        aprsisPacket.trim();
        firstPart = aprsisPacket.substring(0, aprsisPacket.indexOf(","));
        messagePart = aprsisPacket.substring(aprsisPacket.indexOf("::") + 2);
        return firstPart + ",TCPIP,WIDE1-1," + Config.callsign + "::" + messagePart;
    }

    bool processReceivedLoRaMessage(String sender, String packet) {
        String ackMessage, receivedMessage;
        if (packet.indexOf("{") > 0) {     // ack?
            ackMessage = "ack" + packet.substring(packet.indexOf("{") + 1);
            ackMessage.trim();
            //Serial.println(ackMessage);
            for (int i = sender.length(); i < 9; i++) {
                sender += ' ';
            }
            if (Config.beacon.path == "") {
                STATION_Utils::addToOutputPacketBuffer(Config.callsign + ">APLRG1,RFONLY::" + sender + ":" + ackMessage);
            } else {
                STATION_Utils::addToOutputPacketBuffer(Config.callsign + ">APLRG1,RFONLY," + Config.beacon.path + "::" + sender + ":" + ackMessage);
            }

            receivedMessage = packet.substring(packet.indexOf(":") + 1, packet.indexOf("{"));
        } else {
            receivedMessage = packet.substring(packet.indexOf(":") + 1);
        }
        if (receivedMessage.indexOf("?") == 0) {
            delay(2000);
            if (!Config.display.alwaysOn) {
                display_toggle(true);
            }
            STATION_Utils::addToOutputPacketBuffer(QUERY_Utils::process(receivedMessage, sender, "LoRa"));
            if (Config.display.alwaysOn) {
                lastScreenOn = millis();
            }
            show_display(firstLine, secondLine, thirdLine, fourthLine, fifthLine, "Callsign = " + sender, "TYPE --> QUERY", 0);
            return true;
        }
        else {
            return false;
        }
    }

    void processLoRaPacket(String packet) {
        if (espClient.connected()) {
            bool queryMessage = false;
            String aprsPacket, Sender, AddresseeAndMessage, Addressee;
            if (packet != "") {
                if ((packet.substring(0, 3) == "\x3c\xff\x01") && (packet.indexOf("TCPIP") == -1) && (packet.indexOf("NOGATE") == -1) && (packet.indexOf("RFONLY") == -1)) {
                    Sender = packet.substring(3, packet.indexOf(">"));
                    STATION_Utils::updateLastHeard(Sender);
                    Utils::typeOfPacket(aprsPacket, "LoRa-APRS");
                    if (Sender != Config.callsign) {   // avoid listening yourself by digirepeating
                        AddresseeAndMessage = packet.substring(packet.indexOf("::") + 2);
                        Addressee = AddresseeAndMessage.substring(0, AddresseeAndMessage.indexOf(":"));
                        Addressee.trim();

                        if (packet.indexOf("::") > 10 && Addressee == Config.callsign) {      // its a message for me!
                            queryMessage = processReceivedLoRaMessage(Sender, AddresseeAndMessage);
                        }
                        if (!queryMessage) {
                            aprsPacket = buildPacketToUpload(packet);
                            if (!Config.display.alwaysOn) {
                                display_toggle(true);
                            }
                            if (Config.display.alwaysOn) {
                                lastScreenOn = millis();
                            }
                            upload(aprsPacket);
                            Utils::println("---> Uploaded to APRS-IS");
                            STATION_Utils::updateLastHeard(Sender);
                            Utils::typeOfPacket(aprsPacket, "LoRa-APRS");
                            show_display(firstLine, secondLine, thirdLine, fourthLine, fifthLine, sixthLine, seventhLine, 0);
                        }
                    }
                }
            }
        }
    }

    void processAPRSISPacket(String packet) {
        String Sender, AddresseeAndMessage, Addressee, receivedMessage;
        if (!packet.startsWith("#")) {
            if (packet.indexOf("::") > 0) {
                Sender = packet.substring(0, packet.indexOf(">"));
                AddresseeAndMessage = packet.substring(packet.indexOf("::") + 2);
                Addressee = AddresseeAndMessage.substring(0, AddresseeAndMessage.indexOf(":"));
                Addressee.trim();
                if (Addressee == Config.callsign) {             // its for me!
                    if (AddresseeAndMessage.indexOf("{") > 0) {     // ack?
                        String ackMessage = "ack" + AddresseeAndMessage.substring(AddresseeAndMessage.indexOf("{") + 1);
                        ackMessage.trim();
                        delay(4000);
                        for (int i = Sender.length(); i < 9; i++) {
                            Sender += ' ';
                        }
                        String ackPacket = Config.callsign + ">APLRG1,TCPIP,qAC::" + Sender + ":" + ackMessage;// + "\n";
                        upload(ackPacket);
                        receivedMessage = AddresseeAndMessage.substring(AddresseeAndMessage.indexOf(":") + 1, AddresseeAndMessage.indexOf("{"));
                    } else {
                        receivedMessage = AddresseeAndMessage.substring(AddresseeAndMessage.indexOf(":") + 1);
                    }
                    if (receivedMessage.indexOf("?") == 0) {
                        Utils::println("Received Query APRS-IS : " + packet);
                        String queryAnswer = QUERY_Utils::process(receivedMessage, Sender, "APRSIS");
                        //Serial.println("---> QUERY Answer : " + queryAnswer.substring(0,queryAnswer.indexOf("\n")));
                        if (!Config.display.alwaysOn) {
                            display_toggle(true);
                        }
                        if (Config.display.alwaysOn) {
                            lastScreenOn = millis();
                        }
                        delay(500);
                        upload(queryAnswer);
                        SYSLOG_Utils::log("APRSIS Tx", queryAnswer, 0, 0, 0);
                        fifthLine = "APRS-IS ----> APRS-IS";
                        sixthLine = Config.callsign;
                        for (int j = sixthLine.length();j < 9;j++) {
                            sixthLine += " ";
                        }
                        sixthLine += "> " + Sender;
                        seventhLine = "QUERY = " + receivedMessage;
                    }
                } else {
                    Utils::print("Received from APRS-IS  : " + packet);

                    if (Config.aprs_is.toRF && STATION_Utils::wasHeard(Addressee)) {
                        STATION_Utils::addToOutputPacketBuffer(buildPacketToTx(packet));
                        display_toggle(true);
                        if (Config.display.alwaysOn) {
                            lastScreenOn = millis();
                        }
                        Utils::typeOfPacket(packet, "APRS-LoRa");
                    }
                }
                show_display(firstLine, secondLine, thirdLine, fourthLine, fifthLine, sixthLine, seventhLine, 0);
            }
        }
    }

    void listenAPRSIS() {
        if (espClient.connected()) {
            if (espClient.available()) {
                String aprsisPacket = espClient.readStringUntil('\r');
                // Serial.println(aprsisPacket);
                processAPRSISPacket(aprsisPacket);
                lastRxTime = millis();
            }
        }
    }

}
