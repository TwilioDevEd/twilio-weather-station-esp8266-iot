#include "TwilioWeatherStation.hpp"

/* TwilioWeatherStation constructor.
 *  
 * Start the NTP service, initialize the sensors, set up our own
 * preferences, and make the first weather observation.
 * 
 * It's also possible you can get the first alarm after setting it.
 */
TwilioWeatherStation::TwilioWeatherStation(
        const char* ntp_server,
        const int32_t& dht_pin,
        const int32_t& dht_type,
        const int32_t& time_zone_offset_in,
        const int32_t& altitude_in,
        const int32_t& next_alarm_in,
        const char* master_device_number_in,
        const char* twilio_device_number_in,
        const char* unit_type_in,
        const char* twilio_topic_in,
        const char* shadow_topic_in,
        TwilioLambdaHelper& lambdaHelperIn
)
        : lambdaHelper(lambdaHelperIn)
        , ntpUDP()
        , timeClient(
                ntpUDP, 
                ntp_server, 
                time_zone_offset_in*60, 
                UPDATE_NTP_INTERVAL
        )
        , dht(dht_pin, dht_type)
        , bmp(ADAFRUIT_BMP_CONSTANT)
        , time_zone_offset(time_zone_offset_in)
        , location_altitude(altitude_in)
        , master_number(master_device_number_in)
        , twilio_device_number(twilio_device_number_in)
        , unit_type(unit_type_in)
        , last_weather_check(0)
        , shadow_topic(shadow_topic_in)
        , twilio_topic(twilio_topic_in)

{
        last_observation.temperature = 0;
        last_observation.humidity = 0;
        last_observation.pressure = 0;
        last_observation.day = 0;
        last_observation.hour = 0;
        last_observation.minute = 0;
        last_observation.second = 0;
        last_observation.epoch = 0;


        
        dht.begin();
        if(!bmp.begin()){
                lambdaHelper.print_to_serial(
                        "Check your I2C Wiring, we can't access"
                        "the Barometric Pressure Sensor."
                        );
                delay(1000);
        }
        _display_bmp_sensor_details();

        // Start NTP time sync
        timeClient.begin();
        timeClient.update();

        // First weather check
        last_weather_check = millis();

        // Bootstrap the alarm
        next_alarm.timestamp = 0;
        next_alarm.rang = true;
        TwilioWeatherStation::update_alarm(next_alarm_in);

        // Make first weather observation (which may ring the alarm)
        make_observation(last_observation);
        print_observation(last_observation);
}


/* Heartbeat function for Weather Station - update NTP, make observation */
void TwilioWeatherStation::yield()
{
        // This likes to be polled 
        timeClient.update();
        
        if (millis() > last_weather_check + RECHECK_WEATHER_INTERVAL) { 
                lambdaHelper.print_to_serial("BEFORE Remaining Heap Size: ");
                lambdaHelper.print_to_serial(ESP.getFreeHeap());
                lambdaHelper.print_to_serial("\r\n");
                last_weather_check = millis();
                
                make_observation(last_observation);
                print_observation(last_observation);

                lambdaHelper.print_to_serial("AFTER Remaining Heap Size: ");
                lambdaHelper.print_to_serial(ESP.getFreeHeap());
                lambdaHelper.print_to_serial("\r\n");
        }
}


/* Read from the sensors and update our current conditions */
void TwilioWeatherStation::make_observation(WObservation& obs) 
{
        // Read from BMP Sensor
        sensors_event_t event;
        bmp.getEvent(&event);

        float dht_humidity = dht.readHumidity();
        float dht_temperature = dht.readTemperature();

        // Read from DHT Sensor
        if (event.pressure and 
            !(isnan(dht_humidity) or 
                    isnan(dht_temperature))
        ) {                     
                float bmp_temperature;
                bmp.getTemperature(&bmp_temperature);
                float avg_temperature = (dht_temperature + bmp_temperature)/2;

                obs.temperature = avg_temperature;
                obs.humidity = dht_humidity;
                obs.pressure = event.pressure;

                obs.day = timeClient.getDay();
                obs.hour = timeClient.getHours();
                obs.minute = timeClient.getMinutes();
                obs.second = timeClient.getSeconds();
                obs.epoch = timeClient.getEpochTime();

                // Check if we just passed an unrung alarm, but only in the 
                // last 2 weather samples.
                if (!next_alarm.rang) {
                        if (obs.epoch > next_alarm.timestamp and
                            next_alarm.timestamp + \
                            (RECHECK_WEATHER_INTERVAL/1000)*2 > obs.epoch
                        ) {
                                lambdaHelper.print_to_serial(
                                        "We just hit an alarm!\r\n"
                                );
                                _handle_alarm();
                        }
                }

        } else {
                lambdaHelper.print_to_serial(
                        "Sensor errors!  Please check your board."
                );
                lambdaHelper.print_to_serial("\r\n");
                return;
        }
}


/* Dump a lot of weather information to serial (if it exists) */
void TwilioWeatherStation::print_observation(const WObservation& obs) {
        lambdaHelper.print_to_serial("Time is currently: ");
        lambdaHelper.print_to_serial(timeClient.getFormattedTime());
        lambdaHelper.print_to_serial("(");
        lambdaHelper.print_to_serial(obs.epoch);
        lambdaHelper.print_to_serial(")\r\n");
        lambdaHelper.print_to_serial("Timestamp: ");
        lambdaHelper.print_to_serial(obs.day); 
        lambdaHelper.print_to_serial(" ");
        lambdaHelper.print_to_serial(obs.hour); 
        lambdaHelper.print_to_serial(":");
        lambdaHelper.print_to_serial(obs.minute); 
        lambdaHelper.print_to_serial(":");
        lambdaHelper.print_to_serial(obs.second);
        lambdaHelper.print_to_serial(" Pressure: "); 
        lambdaHelper.print_to_serial(obs.pressure); 
        lambdaHelper.print_to_serial(" hPa at sea level, ");
        lambdaHelper.print_to_serial(
                _hpa_to_in_mercury(
                        _hpa_to_sea_level(
                                obs.temperature,
                                obs.pressure,
                                location_altitude
                        )
                )
        ); 
        lambdaHelper.print_to_serial(" inhg at sea level, ");
        lambdaHelper.print_to_serial("Temperature: "); 
        lambdaHelper.print_to_serial(obs.temperature); 
        lambdaHelper.print_to_serial(" *C, "); 
        lambdaHelper.print_to_serial(
                _celsius_to_fahrenheit(obs.temperature)
        ); 
        lambdaHelper.print_to_serial(" *F, ");
        lambdaHelper.print_to_serial("Humidity: "); 
        lambdaHelper.print_to_serial(obs.humidity); 
        lambdaHelper.print_to_serial(" %");
        lambdaHelper.print_to_serial("\r\n");
}


/* The NTP library reports days as an integer, with '0' representing Sunday. */
const char* TwilioWeatherStation::int_to_day(int int_day)
{
        const char* int_to_day[] = {
                "Sun.", "Mon.", "Tue.", "Wed.", "Thu.", "Fri.", "Sat."
        };
        return int_to_day[int_day];
}

/* 
 *  Report current setup, possibly after a power cycle.  We may get
 *  a delta in response
*/
void TwilioWeatherStation::report_shadow_state(const char* topic) 
{
        StaticJsonBuffer<maxMQTTpackageSize> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        JsonObject& state = root.createNestedObject("state");
        JsonObject& reported = state.createNestedObject("reported");

        reported["alarm"] = next_alarm.timestamp;
        reported["units"] = unit_type;
        reported["alt"] = location_altitude;
        reported["tz"] = time_zone_offset;
        reported["t_num"] = twilio_device_number.c_str();
        reported["m_num"] = master_number.c_str();
        
        std::unique_ptr<char []> buffer(new char[maxMQTTpackageSize]());
        root.printTo(buffer.get(), maxMQTTpackageSize);
        lambdaHelper.print_to_serial(buffer.get()); 
        lambdaHelper.publish_to_topic(topic, buffer.get());
}


/* Set a desired shadow state */
void TwilioWeatherStation::update_shadow_state(
        const String& topic,
        const int32_t& new_alarm,
        const String& new_units,
        const int32_t& new_alt,
        const int32_t& new_tz,
        const String& new_tnum,
        const String& new_mnum
)
{
        StaticJsonBuffer<maxMQTTpackageSize> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        JsonObject& state = root.createNestedObject("state");
        JsonObject& reported = state.createNestedObject("desired");

        reported["alarm"] = new_alarm;
        reported["units"] = new_units.c_str();
        reported["alt"] = new_alt;
        reported["tz"] = new_tz;
        reported["t_num"] = new_tnum.c_str();
        reported["m_num"] = new_mnum.c_str();
        
        std::unique_ptr<char []> buffer(new char[maxMQTTpackageSize]());
        root.printTo(buffer.get(), maxMQTTpackageSize);
        lambdaHelper.print_to_serial(buffer.get()); 
        lambdaHelper.publish_to_topic(topic.c_str(), buffer.get());
}


/* When we receive a new shadow update, update all of our preferences */
void TwilioWeatherStation::update_alarm(const int32_t& alarm_in) 
{

        if (next_alarm.timestamp == alarm_in) {
                // Don't reset it, forget it.
                return;
        } 

        /* Check alarm validity */
        if (last_observation.epoch > alarm_in or alarm_in == 0) {
                // This is in the past or turns alarms off.
                next_alarm.rang = true;
        } else {
                // New, future alarm
                next_alarm.rang = false;
        }

        next_alarm.timestamp = alarm_in;
        lambdaHelper.print_to_serial("Alarm updated to: "); 
        lambdaHelper.print_to_serial(next_alarm.timestamp); 
        lambdaHelper.print_to_serial("\r\n"); 
}


/* Change between metric and imperial units */
void TwilioWeatherStation::update_units(String units_in) 
{
        if (units_in.equals("imperial") or units_in.equals("metric")) {
                unit_type = units_in;
                lambdaHelper.print_to_serial("Units updated to: "); 
                lambdaHelper.print_to_serial(unit_type); 
                lambdaHelper.print_to_serial("\r\n"); 
        } else {
                lambdaHelper.print_to_serial(
                        "Unit type must be 'imperial' or 'metric'\r\n"
                        ); 
        }
        
}


/* Change station altitude */
void TwilioWeatherStation::update_alt(const int32_t& alt_in)
{
        location_altitude = alt_in;
        lambdaHelper.print_to_serial("Altitude updated to: "); 
        lambdaHelper.print_to_serial(location_altitude); 
        lambdaHelper.print_to_serial("\r\n"); 
}


/* Update Timezone */
void TwilioWeatherStation::update_tz(const int32_t& tz_in)
{
        time_zone_offset = tz_in;
        lambdaHelper.print_to_serial("Timezone offset set to: "); 
        lambdaHelper.print_to_serial(time_zone_offset); 
        lambdaHelper.print_to_serial("\r\n"); 
        timeClient.setTimeOffset(time_zone_offset*60);
        timeClient.forceUpdate();
}


/* Update Twilio Number of Device */
void TwilioWeatherStation::update_tnum(String tnum_in)
{
        twilio_device_number = tnum_in;
        lambdaHelper.print_to_serial("Device number updated to: "); 
        lambdaHelper.print_to_serial(twilio_device_number); 
        lambdaHelper.print_to_serial("\r\n"); 
}


/* Update Master Number for Alarm */
void TwilioWeatherStation::update_mnum(String mnum_in)
{
        master_number = mnum_in;
        lambdaHelper.print_to_serial("Master number updated to: "); 
        lambdaHelper.print_to_serial(master_number); 
        lambdaHelper.print_to_serial("\r\n"); 
}


/* Craft a nice string containing the current conditions */
String TwilioWeatherStation::get_weather_report(String intro)
{
        // Max size of 160 characters plus termination
        std::unique_ptr<char []> return_body(new char[161]());

        // ESP8266 doesn't support float format strings
        // so we need to convert everything manually.
        std::unique_ptr<char []> temperature(new char[9]());
        std::unique_ptr<char []> humidity(new char[9]());
        std::unique_ptr<char []> pressure(new char[9]());
        std::unique_ptr<char []> pressure_conv(new char[9]());

        float slvl_press = _hpa_to_sea_level(
                last_observation.humidity,
                last_observation.pressure,
                location_altitude
                );

        // Convert to fixed length strings
        dtostrf(last_observation.humidity, 8, 2, humidity.get());
        dtostrf(slvl_press, 8, 2, pressure.get());

        String f_or_c = "C";
        String in_or_mm = "mm";
        
        if (unit_type.equals("imperial")) {
                dtostrf(
                        _celsius_to_fahrenheit(last_observation.temperature), 
                        8, 
                        2, 
                        temperature.get()
                        );
                dtostrf(
                        _hpa_to_in_mercury(slvl_press), 
                        8, 
                        2, 
                        pressure_conv.get()
                        );
                f_or_c = "F";
                in_or_mm = "in";
        } else {
                dtostrf(last_observation.temperature, 8, 2, temperature.get());
                dtostrf(
                        _in_to_mm(_hpa_to_in_mercury(slvl_press)), 
                        8, 
                        2, 
                        pressure_conv.get()
                        );
        }

        snprintf(
                return_body.get(),
                160,
                "%sConditions as of %s %i:%i:%i\n%s *%s\n%s " \
                "% Humidity\n%s hPc (%s %s Hg)\n",
                intro.c_str(),
                TwilioWeatherStation::int_to_day(last_observation.day),
                last_observation.hour,
                last_observation.minute,
                last_observation.second,
                temperature.get(),
                f_or_c.c_str(),
                humidity.get(),
                pressure.get(),
                pressure_conv.get(),
                in_or_mm.c_str()
                );

        return String(return_body.get());
}


/* Dump details of the BMP Sensor */
void TwilioWeatherStation::_display_bmp_sensor_details()
{
      sensor_t sensor;
      bmp.getSensor(&sensor);
      lambdaHelper.print_to_serial("------------------------------------\r\n");
      lambdaHelper.print_to_serial("BMP Sensor:       "); 
      lambdaHelper.print_to_serial(sensor.name);
      lambdaHelper.print_to_serial("\r\n");
      lambdaHelper.print_to_serial("Driver Ver:   "); 
      lambdaHelper.print_to_serial(sensor.version);
      lambdaHelper.print_to_serial("\r\n");
      lambdaHelper.print_to_serial("Unique ID:    "); 
      lambdaHelper.print_to_serial(sensor.sensor_id);
      lambdaHelper.print_to_serial("\r\n");
      lambdaHelper.print_to_serial("Max Value:    "); 
      lambdaHelper.print_to_serial(sensor.max_value); 
      lambdaHelper.print_to_serial(" hPa");
      lambdaHelper.print_to_serial("\r\n");
      lambdaHelper.print_to_serial("Min Value:    "); 
      lambdaHelper.print_to_serial(sensor.min_value); 
      lambdaHelper.print_to_serial(" hPa");
      lambdaHelper.print_to_serial("\r\n");
      lambdaHelper.print_to_serial("Resolution:   "); 
      lambdaHelper.print_to_serial(sensor.resolution); 
      lambdaHelper.print_to_serial(" hPa");
      lambdaHelper.print_to_serial("\r\n");
      lambdaHelper.print_to_serial("------------------------------------\r\n");
      delay(500);
}


/*
 * Function to convert celsius to fahrenheit
 */
inline float TwilioWeatherStation::_celsius_to_fahrenheit(
        const float& celsius
)
{
        return (float)(round((celsius*9/5*1000) + 32000)) / 1000;
}


/*
 * Function to convert hectopascals to inches of mercury
 */
inline float TwilioWeatherStation::_hpa_to_in_mercury(const float& hpa)
{
        return (float)(round(HPA_TO_IN_MERCURY * hpa * 1000)) / 1000;
}   


/*
 * Function to convert hectopascals to inches of mercury
 */
inline float TwilioWeatherStation::_in_to_mm(const float& inches)
{
        return (float)(round(25.4 * inches * 1000)) / 1000;
}


/*
 * Function to convert HPA at our station to HPA at Sea Level
 * 
 * This should be reasonably accurate for most elevations, but for higher 
 * altitudes there is generally a table lookup.  For the United States, that 
 * table would come from the U.S Standard Atmosphere: 
 * https://ccmc.gsfc.nasa.gov/modelweb/atmos/us_standard.html
 */
float TwilioWeatherStation::_hpa_to_sea_level(
    const float& celsius, 
    const float& hpa, 
    const int& altitude
)
{
        // Convert celsius to kelvin
        float kelvin = 273.1 + celsius;

        // Technically, scale height should be the average atmospheric 
        // temperature, but we don't have enough measurements to make a 
        // more accurate guess at the atmospheric temperature.
        float scale_height = 
         (ATM_JOULES_PER_KILOGRAM_KELVIN * kelvin) /    \
                GRAVITATIONAL_ACCELERATION;

        // Observed pressure * exp( altitude / scale_height )
        float adjusted_pressure = \
                hpa * \
                (float)pow(E_CONSTANT, (location_altitude/scale_height));

        return (float)(round(adjusted_pressure * 1000)) / 1000;    
 }


/* Handle passing the alarm epoch time */
void TwilioWeatherStation::_handle_alarm() 
{
        // Attempt to set the alarm one day out
        update_shadow_state(
                shadow_topic,
                next_alarm.timestamp + 86400,
                unit_type,
                location_altitude,
                time_zone_offset,
                twilio_device_number,
                master_number
                );

        // Alarm rang
        next_alarm.rang = true;

        // Text the master number the current conditions
        String weather_string = get_weather_report("Daily Report!\n");

        // Send a weather update from the device number to the master number
        lambdaHelper.send_twilio_message(
                twilio_topic.c_str(),
                master_number,
                twilio_device_number, 
                weather_string,
                String("")
        );
       
}