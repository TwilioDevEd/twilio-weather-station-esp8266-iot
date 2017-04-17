/*
 * Twilio Weather Station, based upon our previous guides on Lambda.
 * 
 * This application demonstrates an ESP8266 Weather Station which sends
 * reports over SMS with Twilio.  It also has the ability to receive text
 * messages, and it will respond with the current conditions.
 * 
 * It also demonstrates receiving an SMS or MMS via AWS API Gateway, Lambda, 
 * and AWS IoT.  An empty response is returned at the Lambda level and the 
 * ESP8266 uses the same path as the sending route to deliver the message.
 * Persistant state is handled by AWS IoT's Device Shadows, which is where
 * we park our preferences.
 * 
 */
 
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>

// AWS WebSocket Client 
#include "AWSWebSocketClient.h"

// Embedded Paho WebSocket Client
#include <MQTTClient.h>
#include <IPStack.h>
#include <Countdown.h>

// Handle incoming messages
#include <ArduinoJson.h>

// Local Includes
#include <DHT.h>
#include <DHT_U.h>
#include "TwilioLambdaHelper.hpp"
#include "TwilioWeatherStation.hpp"


/* 
 *  Pin configuration - set to the pin the DHT sensor is connected to 
 *  and the type of DHT sensor.
*/
#define DHTPIN 0
#define DHTTYPE DHT11


/* IoT/Network Configuration.  Fill these with the values from WiFi and AWS. */
char wifi_ssid[]                = "YOUR NETWORK";
char wifi_password[]            = "NETWORK PW";
char aws_key[]                  = "AWS KEY";
char aws_secret[]               = "AWS SECRET";
char aws_region[]               = "AWS REGION";
char* aws_endpoint              = "AWS ENDPOINT";

/* 
 *  Configurable Preferences - you can set them here, but they are updated via
 *  SMSes from the master number.
 *  
 *  Don't worry about getting them perfectly right, they are updated from the
 *  device shadow state.  You can even skip them if you'd like, but they are 
 *  useful as a reference if you will be deploying multiple stations.
 */
// Set to the numer which can change preferences (your cell?)
char* master_device_number      = "+18005551212";
// Set to a number you own through Twilio (device default)
char* twilio_device_number      = "+18005551212";
// Time zone offset in minutes
int32_t time_zone_offset        = -480;
// Altitude in meters
int32_t location_altitude       = 60;
// Units in metric or imperial units?
char* unit_type                 = "imperial";
// Alarm in seconds since 1970
int32_t alarm                   = 1488508176000;
const char* shadow_topic        = \
        "$aws/things/<YOUR THING NAME>/shadow/update";

/* MQTT, NTP, WebSocket Settings.  You probably do not need to change these. */
const char* delta_topic         = "twilio/delta";
const char* twilio_topic        = "twilio";
int ssl_port = 443;
// NTP Server - it will get UTC, so the whole world can benefit.  However,
// there is no latency adjustment.  Of course, if we're off by a few
// seconds with a weather station it isn't that bad.
const char* ntp_server          = "time.nist.gov";


/* You can use either software, hardware, or no serial port for debugging. */
#define USE_SOFTWARE_SERIAL 1
#define USE_HARDWARE_SERIAL 0


/* Pointer to the serial object, currently for a Sparkfun ESP8266 Thing */
#if USE_SOFTWARE_SERIAL == 1
#include <SoftwareSerial.h>
extern SoftwareSerial swSer(13, 4, false, 256);
Stream* serial_ptr = &swSer;
#elif USE_HARDWARE_SERIAL == 1
Stream* serial_ptr = &Serial;
#else
Stream* serial_ptr = NULL;
#endif


/* Global TwilioLambdaHelper  */
TwilioLambdaHelper lambdaHelper(
        ssl_port,
        aws_region,
        aws_key,
        aws_secret,
        aws_endpoint,
        serial_ptr
);


/* Global TwilioWeatherStation  */
TwilioWeatherStation* weatherStation;


/* 
 * Our Twilio message handling callback.  In this example, Lambda is doing 
 * most of the filtering - if there is a message on the Twilio channel, we want
 * to reply with the current weather conditions the ESP8266 is seeing.
 */
void handle_incoming_message_twilio(MQTT::MessageData& md)
{     
        MQTT::Message &message = md.message;
        std::unique_ptr<char []> msg(new char[message.payloadlen+1]());
        memcpy (msg.get(),message.payload,message.payloadlen);
        StaticJsonBuffer<maxMQTTpackageSize> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(msg.get());
        
        String to_number           = root["To"];
        String from_number         = root["From"];
        String message_body        = root["Body"];
        String message_type        = root["Type"];

        // Only handle messages to the ESP's number
        if (strcmp(to_number.c_str(), twilio_device_number) != 0) {
                return;
        }
        // Only handle incoming messages
        if (!message_type.equals("Incoming")) {
                return;
        }

        // Sending back the current weather to whomever texts the ESP8266
        lambdaHelper.list_message_info(message);
        lambdaHelper.print_to_serial("\n\rNew Message from Twilio!");
        lambdaHelper.print_to_serial("\r\nTo: ");
        lambdaHelper.print_to_serial(to_number);
        lambdaHelper.print_to_serial("\n\rFrom: ");
        lambdaHelper.print_to_serial(from_number);
        lambdaHelper.print_to_serial("\n\r");
        lambdaHelper.print_to_serial(message_body);
        lambdaHelper.print_to_serial("\n\r");

        String weather_string = weatherStation->get_weather_report();
       
        // Send a weather update, reversing the to and from number.
        // So if you copy this line, note the variable switch.
        lambdaHelper.send_twilio_message(
                twilio_topic,
                from_number,
                to_number, 
                weather_string,
                String("")
        );
}


/* 
 * Our device shadow update handler - we'll just dump incoming messages
 * to serial for the weather station.  The 'twilio/delta' channel
 * is where we consume the pared down messages.
 * 
 * We do _post_ to this channel however, to get the initial device
 * shadow on a power cycle and to update the alarm after it goes off.
 */
void handle_incoming_message_shadow(MQTT::MessageData& md)
{
        MQTT::Message &message = md.message;
        
        lambdaHelper.list_message_info(message);
        lambdaHelper.print_to_serial("Current Remaining Heap Size: ");
        lambdaHelper.print_to_serial(ESP.getFreeHeap());

        std::unique_ptr<char []> msg(new char[message.payloadlen+1]());
        memcpy (msg.get(), message.payload, message.payloadlen);

        lambdaHelper.print_to_serial(msg.get());
        lambdaHelper.print_to_serial("\n\r");
}


/* Setup function for the ESP8266 Amazon Lambda Twilio Example */
void setup() 
{
        WiFi.begin(wifi_ssid, wifi_password);
    
        #if USE_SOFTWARE_SERIAL == 1
        swSer.begin(115200);
        #elif USE_HARDWARE_SERIAL == 1
        Serial.begin(115200);
        #endif
        
        while (WiFi.status() != WL_CONNECTED) {
                delay(1000);
                lambdaHelper.print_to_serial(".\r\n");
        }

        lambdaHelper.print_to_serial("Connected to WiFi, IP address: ");
        lambdaHelper.print_to_serial(WiFi.localIP());
        lambdaHelper.print_to_serial("\n\r");

        // See note in TwilioWeatherStation.hpp - the reference to lambdaHelper 
        // is questionable in C++, but we include it here so you can see how 
        // the infrastructure has evolved from our previous examples.
        weatherStation = new TwilioWeatherStation(
                ntp_server,
                DHTPIN,
                DHTTYPE,
                time_zone_offset,
                location_altitude,
                alarm,
                master_device_number,
                twilio_device_number,
                unit_type,
                twilio_topic,
                shadow_topic,
                lambdaHelper
        );

        // Connect to MQTT over Websockets.
        if (lambdaHelper.connectAWS()){
                lambdaHelper.subscribe_to_topic(
                        shadow_topic, 
                        handle_incoming_message_shadow
                );
                lambdaHelper.subscribe_to_topic(
                        delta_topic, 
                        handle_incoming_message_delta
                );
                lambdaHelper.subscribe_to_topic(
                        twilio_topic, 
                        handle_incoming_message_twilio
                );
                weatherStation->report_shadow_state(shadow_topic);         
        }

        // Yield to the Weather Station heartbeat function.
        weatherStation->yield();
        
}


/* 
 * Our Twilio update delta handling callback.  We look for updated parameters
 * and any that we spot we send to the weatherStation for update.
 */
void handle_incoming_message_delta(MQTT::MessageData& md)
{     
        MQTT::Message &message = md.message;
        std::unique_ptr<char []> msg(new char[message.payloadlen+1]());
        memcpy (msg.get(),message.payload,message.payloadlen);

        // List some info to serial
        lambdaHelper.list_message_info(message);
        lambdaHelper.print_to_serial(msg.get());
        lambdaHelper.print_to_serial("\n\r");
        
        
        StaticJsonBuffer<maxMQTTpackageSize> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(msg.get());
        
        if (root["state"]["alarm"].success()) {
                int32_t possible_alarm = root["state"]["alarm"];
                weatherStation->update_alarm(possible_alarm);
        }
        if (root["state"]["units"].success()) {
                String possible_units = root["state"]["units"];
                weatherStation->update_units(possible_units);
                //delete[] possible_units;
        }
        if (root["state"]["alt"].success()) {
                int possible_alt = root["state"]["alt"];
                weatherStation->update_alt(possible_alt); 
        }
        if (root["state"]["tz"].success()) {
                int possible_tz = root["state"]["tz"];
                weatherStation->update_tz(possible_tz); 
        }
        if (root["state"]["t_num"].success()) {
                String possible_tnum = root["state"]["t_num"];
                weatherStation->update_tnum(possible_tnum);
                //delete[] possible_tnum;
        }
        if (root["state"]["m_num"].success()) {
                String possible_mnum = root["state"]["m_num"];
                weatherStation->update_mnum(possible_mnum);
                //delete[] possible_mnum;
        }

        // Let AWS IoT know the updated state
        weatherStation->report_shadow_state(shadow_topic);
        
}


/* 
 * Our loop checks that the AWS Client is still connected, and if so calls its
 * yield() function encapsulated in lambdaHelper.  If it isn't connected, the 
 * ESP8266 will attempt to reconnect. 
 */
void loop() {
        if (lambdaHelper.AWSConnected()) {
                lambdaHelper.handleRequests();
        } else {
                // Handle reconnection if necessary.
                if (lambdaHelper.connectAWS()){
                        lambdaHelper.subscribe_to_topic(
                                shadow_topic, 
                                handle_incoming_message_shadow
                        );
                        lambdaHelper.subscribe_to_topic(
                                delta_topic, 
                                handle_incoming_message_delta
                        );
                        lambdaHelper.subscribe_to_topic(
                                twilio_topic, 
                                handle_incoming_message_twilio
                        );
                        weatherStation->report_shadow_state(shadow_topic);
                }
        }
        
        /* Time and weather checking heartbeat */
        weatherStation->yield();
}