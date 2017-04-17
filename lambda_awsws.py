"""
Handle 'help' and 'set' for a weather station.

Here we show the infrastructure for a weather station run on AWS IoT, API
Gateway, and Lambda.  We handle 'set' and 'help' messages directly in Lambda
and either change settings or return assistance.

As a webhook comes in, we'll verify the webhook is from Twilio then extract the
key information and send to our IoT device(s) subscribed to the 'twilio'
topic.  Devices on that channel will then react, either confirming a changed
setting or replying with the weather.
"""
from __future__ import print_function

import json
import os
import boto3
import urllib
import time
import datetime
import calendar
from datetime import timedelta
from twilio.request_validator import RequestValidator
from twilio import twiml

# The six preferences supported in the demo application
topic_list = ["alt", "tz", "m_num", "t_num", "alarm", "units"]


def ret_int(potential):
    """Utility function to check the input is an int, including negative."""
    try:
        return int(potential)
    except:
        return None


def handle_help(body, from_number):
    """
    Handle Help for incoming SMSes.

    Remind the user how to interact with the weather station, to be able to
    change settings on the fly.  We also demonstrate reporting back the
    Device Shadow from AWS IoT, and redact the sensitive information
    (For this demo App, it is 'Master Number')
    """
    r = twiml.Response()
    word_list = body.split(' ')

    if (len(word_list) < 2 or
            (word_list[1].lower() not in topic_list and
                word_list[1].lower() != "cur")):
        our_response = \
            ":: Help (var)\n" \
            "alt - Altitude\n" \
            "cur - Current Set\n" \
            "m_num - Master number\n" \
            "t_num - Twilio Number\n" \
            "alarm - Alarm\n" \
            "units - Units\n" \
            "tz - Timezone"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "alt":
        our_response = \
            ":: Help alt\n" \
            "Set altitude in meters (integers):\n" \
            "set alt 50\n"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "tz":
        our_response = \
            ":: Help tz\n" \
            "Set timezone adjust in minutes:\n" \
            "set tz -480\n"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "m_num":
        our_response = \
            ":: Help m_num\n" \
            "Set master number:\n" \
            "set m_num +18005551212\n"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "t_num":
        our_response = \
            ":: Help t_num\n" \
            "Set Twilio number:\n" \
            "set t_num +18005551212\n"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "alarm":
        our_response = \
            ":: Help alarm\n" \
            "Set alarm hours:minutes, 24 hour clock:\n" \
            "set alarm 15:12\n"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "units":
        our_response = \
            ":: Help units\n" \
            "Set units type:\n" \
            "set units imperial\nor\n" \
            "set units metric"
        r.message(our_response)
        return str(r)

    if word_list[1].lower() == "cur":
        aws_region = os.environ['AWS_IOT_REGION']
        client = boto3.client('iot-data', region_name=aws_region)

        aws_response = client.get_thing_shadow(
            thingName=os.environ['THING_NAME']
        )
        from_aws = {}
        if u'payload' in aws_response:
            our_response = ""
            from_aws = json.loads(aws_response[u'payload'].read())

            if u'state' in from_aws and u'desired' in from_aws[u'state']:
                desired = from_aws[u'state'][u'desired']
                if u'tz' in desired:
                    our_response += 'tz: ' + str(desired[u'tz']) + '\n'
                if u't_num' in desired:
                    our_response += 't_num: ' + str(desired[u't_num']) + '\n'
                if u'm_num' in desired and from_number == desired[u'm_num']:
                    our_response += 'm_num: ' + str(desired[u'm_num']) + '\n'
                if u'm_num' in desired and from_number != desired[u'm_num']:
                    our_response += 'm_num: (not this number)\n'
                if u'alarm' in desired:
                    our_response += 'alarm: ' + str(desired[u'alarm']) + '\n'
                if u'units' in desired:
                    our_response += 'units: ' + str(desired[u'units']) + '\n'
                if u'alt' in desired:
                    our_response += 'alt: ' + str(desired[u'alt']) + '\n'
            else:
                our_response = \
                    "No shadow set, set it through AWS IoT.\n"
                r.message(our_response)
                return str(r)
        else:
                our_response = \
                    "No Thing found, set it up through AWS IoT.\n"
                r.message(our_response)
                return str(r)

        r.message(our_response)
        return str(r)

    r.message("Did not understand, try '?' perhaps?")
    return str(r)


def handle_set(body, from_number):
    """
    Handle Changing preferences with incoming SMSes.

    This function will parse (crudely) an incoming SMS message and update
    the current device shadow state with the new setting.  We do some very
    basic checking, - in production you'll want to do much more extensive
    parsing and checking here.

    Additionally, we demonstrate very basic security - only the Master Number
    can update this Thing.
    """
    r = twiml.Response()
    word_list = body.split(' ')

    aws_region = os.environ['AWS_IOT_REGION']
    client = boto3.client('iot-data', region_name=aws_region)

    aws_response = client.get_thing_shadow(
        thingName=os.environ['THING_NAME']
    )

    # Check this person is authorized to change things.
    time_zone_shadow = 0
    if u'payload' in aws_response:
        from_aws = json.loads(aws_response[u'payload'].read())

        if u'state' in from_aws and u'desired' in from_aws[u'state']:
            desired = from_aws[u'state'][u'desired']
            if u'm_num' in desired and from_number == desired[u'm_num']:
                # This person _is_ authorized to make changes.
                # Put any extra logic here you need from the shadow state.
                time_zone_shadow = int(desired['tz'])
            elif u'm_num' in desired and from_number != desired[u'm_num']:
                # This person _is not_ authorized to make changes
                # Put any handling logic here
                our_response = "UNAUTHORIZED!"
                r.message(our_response)
                return str(r)

            # If no Shadow or no master number set, we'll fall through.
            pass

    # Trap invalid 'sets' and make sure we have a 0, 1, and 2 index.
    if len(word_list) != 3 or word_list[1] not in topic_list:
        our_response = \
            "Should be exactly 3 terms:\n" \
            "set <term> <preference>\n" \
            "Perhaps see help with '?'?"
        r.message(our_response)
        return str(r)

    print(word_list[0], word_list[1], word_list[2])

    # Set altitude
    if word_list[1].lower() == "alt":
        # Clean HTML Characters
        word_list[2] = word_list[2].encode('ascii', 'ignore')
        if ret_int(word_list[2]) is None:
            our_response = \
                "Altitude must be an integer, in meters.\n"
            r.message(our_response)
            return str(r)
        else:
            new_alt = ret_int(ord_list[2])
            from_aws[u'state'][u'desired'][u'alt'] = new_alt
            our_response = \
                "Updating altitude to " + str(new_alt) + "m.\n"
            r.message(our_response)

            client.update_thing_shadow(
                thingName=os.environ['THING_NAME'],
                payload=json.dumps(from_aws)
            )

            return str(r)

    # Set timezone
    if word_list[1].lower() == "tz":
        # Clean HTML Characters
        word_list[2] = word_list[2].encode('ascii', 'ignore')
        if ret_int(word_list[2]) is None:
            our_response = \
                "Timezone must be an integer, in minutes.\n"
            r.message(our_response)
            return str(r)
        else:
            new_tz = ret_int(word_list[2])

            from_aws[u'state'][u'desired'][u'tz'] = new_tz
            our_response = \
                "Updating timezone to " + str(new_tz) + " min.\n"
            r.message(our_response)

            client.update_thing_shadow(
                thingName=os.environ['THING_NAME'],
                payload=json.dumps(from_aws)
            )

            return str(r)

    if word_list[1].lower() == "m_num":
        new_mnum = word_list[2]

        if not word_list[2].startswith(u"+"):
            our_response = \
                "Number must start with '+' followed by county + local " \
                "code then phone number ie '+18005551212\n"
            r.message(our_response)
            return str(r)

        else:
            from_aws[u'state'][u'desired'][u'm_num'] = new_mnum
            our_response = \
                "Updating master number to " + str(new_mnum) + ".\n"
            r.message(our_response)

            client.update_thing_shadow(
                thingName=os.environ['THING_NAME'],
                payload=json.dumps(from_aws)
            )

            return str(r)

    if word_list[1].lower() == "t_num":
        new_tnum = word_list[2]

        if not word_list[2].startswith(u"+"):
            our_response = \
                "Number must start with '+' followed by county + local " \
                "code then phone number ie '+18005551212'\n"
            r.message(our_response)
            return str(r)

        else:
            from_aws[u'state'][u'desired'][u't_num'] = new_tnum
            our_response = \
                "Updating Twilio number to " + str(new_tnum) + \
                ".  Update webhook in Twilio console too!\n"
            r.message(our_response)

            client.update_thing_shadow(
                thingName=os.environ['THING_NAME'],
                payload=json.dumps(from_aws)
            )

            return str(r)

    if word_list[1].lower() == "alarm":
        split_alarm = word_list[2].split(":")
        if (len(split_alarm) != 2 or
                ret_int(split_alarm[0]) is None or
                ret_int(split_alarm[1]) is None):
            our_response = \
                "Alarm must be in XX:YY format, will adjust to local" \
                " timezone automatically.\n"
            r.message(our_response)
            return str(r)
        else:
            epoch_time = int(time.time())
            # ESP's library uses a _local_ timestamp
            epoch_time += 60 * time_zone_shadow

            print("Time is " + str(epoch_time))

            current_datetime = datetime.date.today()
            proposed_time = datetime.datetime(
                current_datetime.year,
                current_datetime.month,
                current_datetime.day,
                ret_int(split_alarm[0]),
                ret_int(split_alarm[1])
            )

            # proposed_time += timedelta(minutes=((-1) * time_zone_shadow))
            proposed_epoch = calendar.timegm(proposed_time.timetuple())
            print("Proposed: " + str(proposed_epoch))

            if proposed_epoch < epoch_time:
                # This already passed today, use tomorrow.
                proposed_time += timedelta(days=1)
                proposed_epoch = calendar.timegm(proposed_time.timetuple())
                print("New Proposed: " + str(proposed_epoch))

            our_response = \
                "Updating alarm to " + str(proposed_epoch) + "."
            r.message(our_response)

            from_aws[u'state'][u'desired'][u'alarm'] = proposed_epoch
            client.update_thing_shadow(
                thingName=os.environ['THING_NAME'],
                payload=json.dumps(from_aws)
            )
            return str(r)

    if word_list[1].lower() == "units":
        if word_list[2].lower() not in [u'imperial', u'metric']:
            our_response = \
                "Must be 'imperial' or 'metric' units, sans quotes."
            r.message(our_response)
            return str(r)
        else:
            new_units = word_list[2].lower()

            from_aws[u'state'][u'desired'][u'units'] = new_units
            our_response = \
                "Updating units to " + str(new_units) + "."
            r.message(our_response)

            client.update_thing_shadow(
                thingName=os.environ['THING_NAME'],
                payload=json.dumps(from_aws)
            )

            return str(r)

    r.message("Did not understand, try '?' perhaps?")
    return str(r)


def twilio_webhook_handler(event, context):
    """
    Main entry function for Twilio Webhooks.

    This function is called when a new Webhook coems in from Twilio,
    pointed at the associated API Gateway.  We divide messages into three
    buckets:

    1) 'Help' messages
    2) 'Set' messages
    3) 'Catch-all'/default, which we forward along to the weather station for
       an update of conditions.
    """

    print("Received event: " + str(event))
    null_response = '<?xml version=\"1.0\" encoding=\"UTF-8\"?>' + \
                    '<Response></Response>'

    # Trap no X-Twilio-Signature Header
    if u'twilioSignature' not in event:
        print("NO HEADER")
        return null_response

    form_parameters = {
        k: urllib.unquote_plus(v) for k, v in event.items()
        if k != u'twilioSignature'
    }

    validator = RequestValidator(os.environ['AUTH_TOKEN'])
    request_valid = validator.validate(
        os.environ['REQUEST_URL'],
        form_parameters,
        event[u'twilioSignature']
    )

    # Trap invalid requests not from Twilio
    if not request_valid:
        print("NOT VALID")
        return null_response

    # Trap fields missing
    if u'Body' not in form_parameters or u'To' not in form_parameters \
            or u'From' not in form_parameters:
        print("MISSING STUFF")
        return null_response

    body = form_parameters[u'Body']

    # Determine the type of incoming message...
    if ((len(body) > 0 and
            body[0] == u'?') or
        (len(body) > 3 and
            body.lower().startswith(u'help'))):
        # This is for handling 'Help' messages.

        # Note that Twilio will catch the default 'HELP', so we also need to
        # alias it to something else, like '?'.  We also add a help
        # response for each possible setting.
        # See: https://support.twilio.com/hc/en-us/articles/223134027-Twilio-support-for-STOP-BLOCK-and-CANCEL-SMS-STOP-filtering-
        # Or: https://support.twilio.com/hc/en-us/articles/223181748-Customizing-HELP-STOP-messages-for-SMS-filtering
        return handle_help(
            form_parameters[u'Body'],
            form_parameters[u'From']
        )

    elif ((len(body) > 1 and
            body[0].lower() == u's' and
            body[1] == u' ') or
          (len(form_parameters[u'Body']) > 3 and
            body.lower().startswith(u'set') and
            body[3] == u' ')):
        # This is for handling 'Set' messages.

        # Again, we also handle an alias of 's', and have a handler for each
        # preference.
        return handle_set(
            form_parameters[u'Body'],
            form_parameters[u'From']
        )

    else:

        aws_region = os.environ['AWS_IOT_REGION']
        aws_topic = os.environ['AWS_TOPIC']
        client = boto3.client('iot-data', region_name=aws_region)

        client.publish(
            topic=aws_topic,
            qos=0,
            payload=json.dumps({
                "To": form_parameters[u'To'],
                "From": form_parameters[u'From'],
                "Body": "Give me some weather!",
                "Type": "Incoming"
            })
        )

        # A blank response informs Twilio not to take any actions.
        # Since we are reacting asynchronously, if we are to respond
        # it will come from the weather station.
        return null_response
