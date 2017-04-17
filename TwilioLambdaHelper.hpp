#pragma once

#include "TwilioLambdaHelper.hpp"

// Normally we'd wrap the Helper and it's actually not required to declare 
// these externs, but to see where they come from and to see what changed from 
// the previous guide we'll leave these declarations.
extern const int maxMQTTpackageSize;
extern const int maxMQTTMessageHandlers;

#include <Adafruit_BMP085_U.h>
#include <DHT.h>
#include <DHT_U.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

/* Weather and Constant Definitions */
#define HPA_TO_IN_MERCURY               .0295299830714
#define SEA_LEVEL_PRESSURE_HPA          1013.25
#define GRAVITATIONAL_ACCELERATION      9.807
#define ATM_JOULES_PER_KILOGRAM_KELVIN  287.1
#define E_CONSTANT                      2.718281828182
#define ADAFRUIT_BMP_CONSTANT           10180

// X minutes at 60000 ticks per minute
#define UPDATE_NTP_INTERVAL             10*60*1000
// Every 3 minutes
#define RECHECK_WEATHER_INTERVAL        3*60*1000 

/* 
 *  Weather observation struct.  Not sure if you would like to expand
 *  this, so it is separate from the TWS class.
 *  
 *  (4 bytes * 3) + 1 + 1 + 1 + 1 + 8 = 24 Bytes each as it is.
 *  On my board there are ~ 17-18 KiB free 
 */
struct WObservation {
        /* Temperature in Celsius */
        float           temperature;

        /* Humidity Percentage */
        float           humidity;

        /* Pressure in hPA, adjusted to Sea Level  */
        float           pressure;

        /* Timestamp fields - 4 bytes total */
        uint8_t         day;
        uint8_t         hour;
        uint8_t         minute;
        uint8_t         second;

        /* 
         * Epoch time (for comparisons) 
         * Match the UNIX type, even though we'll rollover in 2038
         */
        int32_t        epoch;       
};



/*
 * The TwilioWeatherStation class simplifies the handling of some of the 
 * necessary functions for reporting the weather.
 * 
 * We're encapsulating timekeeping, polling the sensors and updating the 
 * preferences to keep the .ino file relatively uncluttered.  
 */
class TwilioWeatherStation {
public:
        TwilioWeatherStation(
                const char* ntp_server,
                const int& dht_pin,
                const int& dht_type,
                const int32_t& time_zone_offset_in,
                const int& altitude_in,
                const int32_t& next_alarm_in,
                const char* master_device_number_in,
                const char* twilio_device_number_in,
                const char* unit_type_in,
                const char* twilio_topic_in,
                const char* shadow_topic_in,
                TwilioLambdaHelper& lambdaHelperIn
        );

        /* Heartbeat function - every loop we need to do maintenance in here */
        void yield();

        /* Return contents of last sensor check. */
        String get_weather_report(String intro="");

        /* Check the sensors and print the latest check */
        void make_observation(WObservation& obs);
        void print_observation(const WObservation& obs);

        /* Getters and Setters */
        void update_alarm(const int32_t& alarm_in);
        void update_units(String units_in);
        void update_alt(const int32_t& alt_in);
        void update_tz(const int32_t& tz_in);
        void update_tnum(String tnum_in);
        void update_mnum(String mnum_in);

        /* Report current shadow state (and possibly get a delta) */
        void report_shadow_state(const char* topic);

        /* Set a desired shadow state, new alarms, etc. */
        void update_shadow_state(
                const String& topic,
                const int32_t& new_alarm,
                const String& new_units,
                const int32_t& new_alt,
                const int32_t& new_tz,
                const String& new_tnum,
                const String& new_mnum
        );

        /* Int to day string mapping */
        static const char* int_to_day(int int_day);
        
private:
        void _display_bmp_sensor_details();
        void _handle_alarm();
        float _celsius_to_fahrenheit(const float& celsius);
        float _hpa_to_in_mercury(const float& hpa);
        float _hpa_to_sea_level(
            const float& celsius, 
            const float& hpa, 
            const int& altitude
            );
        float _in_to_mm(const float& inches);

        /* 
         *  We're keeping a TwilioLambdaHelper reference to 
         *  show how the code differs from the previous guides.  In the 
         *  embedded world we can cheat - if the reference no longer exists 
         *  we've either got larger problems... or the power was cut.
         *  
         *  Don't do this on the desktop!!!
         */
        TwilioLambdaHelper&             lambdaHelper;

        /* Sensors and Timekeeping */
        WiFiUDP                         ntpUDP;
        NTPClient                       timeClient;
        DHT                             dht;
        Adafruit_BMP085_Unified         bmp;

        /* Most recent weather observation and time */
        WObservation                    last_observation;
        uint64_t                        last_weather_check;

        /* Next alarm */
        struct Alarm {
                int32_t timestamp;
                bool rang;
        } next_alarm;

        /* Preferences */
        int32_t location_altitude;
        int32_t time_zone_offset;
        String master_number;
        String twilio_device_number;
        String unit_type;
        String shadow_topic;
        String twilio_topic;

};