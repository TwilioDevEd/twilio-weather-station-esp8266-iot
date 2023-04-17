# twilio-weather-station-esp8266-iot
> This repository is archived and no longer maintained. The companion blog post is [still available on the Twilio blog](https://www.twilio.com/blog/twilio-weather-station-amazon-aws-iot-lambda-esp8266).

---

Build a weather station with an ESP8266, AWS IoT, and Twilio.

An example application building a weather station with an ESP8266 and Amazon IoT, Lambda, and Twilio.  You can set preferences from a master number, and get the most recent weather report at any time.  Our companion blog post is located here:

https://www.twilio.com/blog/twilio-weather-station-amazon-aws-iot-lambda-esp8266

The ESP8266 will connect account to an AWS IoT Account on the MQTT topic 'twilio'.  When users SMS (or MMS) the station, it will reply with the current weather conditions.  It can also handle preference updates over the air for the following settings:

* Units (Imperial vs. Metric)
* Altitude
* Timezone offset from UTC
* An outgoing phone number
* A 'master' phone number
* An alarm

On the hardware side, you'll need:
* ESP8266
* BMP180 Pressure Sensor (or BMP##)
* DHT11 Humidity Sensor (or DHT##)

For a complete writeup on using Twilio with Amazon Web Services, see these four articles on Twilio's documentation site where we'll take you from beginner to AWS ecosystem master:
* https://www.twilio.com/docs/guides/receive-reply-sms-mms-messages-using-amazon-api-gateway-lambda
* https://www.twilio.com/docs/guides/secure-amazon-lambda-python-app-validating-incoming-twilio-requests
* https://www.twilio.com/docs/guides/reply-sms-messages-esp8266-amazon-aws-iot-lambda-and-api-gateway
* https://www.twilio.com/docs/guides/send-sms-or-mms-messages-esp8266-amazon-aws-iot-lambda

## Build example:


### AWS Lambda, IoT, Gateway Explorer
You will need an AWS account.  Set up Lambda IoT and add the security necessary for your ESP8266.

Note: Both Lambda functions require the Twilio Python helper library.  To see how to install external libraries on Lambda, see [this article](http://docs.aws.amazon.com/lambda/latest/dg/lambda-python-how-to-create-deployment-package.html). Also see this Repo: https://github.com/TwilioDevEd/Twilio-AWS-IoT-ESP8266-Example

For sending SMS/MMS messages, use IoT on the 'twilio' channel as a trigger with the SQL of "SELECT * FROM 'twilio' WHERE Type='Outgoing'".  You must not use SQL version 2016-3-23(!), it will not work with null-terminated strings!

For receiving messages, use API Gateway and pass through form parameters.  Return the empty response to Twilio with application/xml.  The 'response' will come from a new 'send' originating on the ESP8266.


### ESP8266
<pre>
git clone https://github.com/TwilioDevEd/twilio-weather-station-esp8266-iot.git
</pre>

#### Install the following packages with the Arduino Package Manager:
* Adafruit BMP085 Unified
* Adafruit Unified Sensor
* DHT Sensor Library
* NTPClient
* ArduinoJSON
* WebSockets

#### Install the following packages [manually/by ZIP](https://www.arduino.cc/en/guide/libraries#toc5)
* [AWS-MQTT-WebSockets](https://github.com/odelot/aws-mqtt-websockets) by odelot
* [Eclipse Paho Arduino Client](https://projects.eclipse.org/projects/technology.paho/downloads)
* [ESP8266 AWS SDK for Arduino IDE](https://www.twilio.com/docs/documents/21/aws-sdk-arduino-esp8266.zip)

Open 'twilio-weather-station-esp8266-iot' in Arduino IDE

## Edit the credentials, change desired settings...
Compile and Upload to the board!

## Run example:
(Should send an MMS automatically when uploaded to ESP8266 or power is restored)

Text it some nonsense and it'll send a report!

Look in the Python file to see what other options we've added for help/setting variables.

## Motivations

To show how to use Twilio SMS capabilities plus the AWS ecosystem to do remote monitoring!  Hopefully the infrastructure details of this article help you build your own _Thing_.
## Meta & Licensing

* [MIT License](http://www.opensource.org/licenses/mit-license.html)
* Lovingly crafted by Twilio Developer Education.
