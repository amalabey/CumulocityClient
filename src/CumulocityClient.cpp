#include "Arduino.h"
#include "CumulocityClient.h"
#include <stdlib.h>
#include <string.h>

CumulocityClient::CumulocityClient(Client& networkClient, char* deviceId) {

    Serial.printf("CumulocityClient(%s)\n", deviceId);

    this->_client = PubSubClient(networkClient);
    this->_deviceId = deviceId;
    this->_client.setKeepAlive(_keepAlive);
}

bool CumulocityClient::reconnect() {

    if (_credentials.tenant != NULL && _credentials.username != NULL && _credentials.password != NULL) {
        return connectClient();
    } else {
        return false;
    }
}

bool CumulocityClient::connect(char* host, char* tenant, char* user, char* password) {
    return connect(host, 1883, tenant, user, password);
}

bool CumulocityClient::connect(char* host, uint16_t port, char* tenant, char* user, char* password) {
    Serial.printf("connect(%s,%s,%s)\n", host, tenant, user);

    _host = host;
    _port = port;
    _credentials.tenant = tenant;
    _credentials.username = user;
    _credentials.password = password;

    String myClientId = "d:"; 
    myClientId += _deviceId;

    _clientId = (char*) malloc(myClientId.length() +1);
    strcpy(_clientId,myClientId.c_str());

    _client.setServer(_host, _port);

    return connectClient();

}

bool CumulocityClient::connect(char* host, char* tenant, char* user, char* password, char* defaultTemplate) {
    return connect(host, 1883, tenant, user, password, defaultTemplate);
}

bool CumulocityClient::connect(char* host, uint16_t port, char* tenant, char* user, char* password, char* defaultTemplate) {

    _host = host;
    _port = port;
    _credentials.tenant = tenant;
    _credentials.username = user;
    _credentials.password = password;

    String myClientId = "d:";
    myClientId += _deviceId; 
    myClientId += ":";
    myClientId += defaultTemplate; 

    _clientId = (char*) malloc(myClientId.length() +1);
    strcpy(_clientId,myClientId.c_str());

    _client.setServer(_host, _port);

    return connectClient();

}

bool CumulocityClient::connectClient() {

    Serial.println("ConnectClient()");

    _client.setCallback(
        [this](char* topic, byte* payload, unsigned int length){
            this->callbackHandler(topic, payload, length);
        }
    );

    String user = _credentials.username;
    if(user.indexOf("/") <= 0)
    {
        user = _credentials.tenant;
        user += "/";
        user += _credentials.username;
    }

    bool success = 
        _client.connect(_clientId, user.c_str(), _credentials.password, "s/us", 0, 
        false, "400,c8y_ConnectionEvent,\"Connections lost.\"", true);

    if (!success) {

        Serial.print("Unable to connect to Cumulocity as ");
        Serial.println(user);
        Serial.println(_client.state());
    } else {
        Serial.println("Connected to cumulocity.");
    }

    return success;
}

void CumulocityClient::disconnect() {

	Serial.print("disconnect()");
	_client.disconnect();
}

void CumulocityClient::retrieveDeviceCredentials() {

	_credentialsReceived = false;
	_client.subscribe("s/dcr");
	_client.publish("s/ucr", "");
}

bool CumulocityClient::checkCredentialsReceived() {

	if (_credentialsReceived) {
		_client.unsubscribe("s/dcr");
	} else {
		_client.publish("s/ucr", "");
		_client.loop();
	}

	return _credentialsReceived;
}

Credentials CumulocityClient::getCredentials() {

	return _credentials;
}

void CumulocityClient::setDeviceCredentials(char* tenant, char* user, char* password) {
    _credentials.tenant = tenant;
    _credentials.username = user;
    _credentials.password = password;
}

void CumulocityClient::registerDevice(char* deviceName, char* deviceType){

	Serial.printf("registerDevice(%s, %s)\n", deviceName, deviceType);

	char mqttMessage[1024];
	// = (char*) malloc(strlen(_deviceId) + strlen(deviceType)+6);
	sprintf(mqttMessage, "100,%s,%s", deviceName, deviceType);

	_client.publish("s/us", mqttMessage);
}

void CumulocityClient::createMeasurement(char* fragment, char* series, char* value, char* unit) {

	char mqttMessage[2048];//  = (char*) malloc(8+ strlen(fragment)+ strlen(series) + strlen(value) + strlen(unit));
	sprintf(mqttMessage, "200,%s,%s,%s,%s", fragment, series, value, unit);
	_client.publish("s/us", mqttMessage);

}

void CumulocityClient::loop() {

    bool myConnected = _client.loop();

    if (!myConnected) {
        reconnect();
    }
}

void CumulocityClient::setKeepAlive(int keepAlive) {

    _keepAlive = keepAlive;
}

void CumulocityClient::setDeviceId(char* deviceId) {
    _deviceId = deviceId;
    
    String myClientId = "d:"; 
    myClientId += _deviceId;
    
    _clientId = (char*) malloc(myClientId.length() +1);
    strcpy(_clientId,myClientId.c_str());
}

void CumulocityClient::setDeviceId(char* deviceId, char* defaultTemplate) {
    _deviceId = deviceId;
        
    String myClientId = "d:";
    myClientId += _deviceId; 
    myClientId += ":";
    myClientId += defaultTemplate;

    _clientId = (char*) malloc(myClientId.length() +1);
    strcpy(_clientId,myClientId.c_str());
}

void CumulocityClient::setSupportedOperations(char* operations) {
    
    Serial.printf("setSupportedOperations(%s)\n", operations);

    String myOperations = "114,";
    myOperations += operations;
	_client.publish("s/us", myOperations.c_str());
    _client.subscribe("s/ds");
}

void CumulocityClient::getPendingOperations() {

    Serial.printf("getPendingOperations()\n");

    _client.publish("s/us", "500");
}

void CumulocityClient::setCallback(C8Y_CALLBACK_SIGNATURE) {

    Serial.printf("setCallback()\n");

    this->callback = callback;
}

void CumulocityClient::callbackHandler(char* topic, byte* payload, unsigned int length) {

	Serial.printf("callbackHandler(topic: %s, payload: %s)\n", topic, payload);

	//int firstComma =
	char myPayload[length+1];

	strncpy(myPayload, (char*)payload,length);
	myPayload[length] = '\0';

	if (strcmp(topic, "s/dcr") == 0 && length > 0) {
		Serial.print("Device credentials received: ");
		Serial.println(myPayload);
		parseCredentials(myPayload);
		_credentialsReceived = true;
	} else if (strcmp(topic, "s/ds") == 0 && length > 0) {
        if (callback) {

            handleOperation(myPayload);
        }
    }

}

void CumulocityClient::handleOperation(char* payload) {

    char** elements = parseCSV(payload);

    char* templateCode = elements[0];
    char* clientId = elements[1];
    char* content = elements[2];
    
    free(elements);

    Serial.printf("templatecode: %s, clientId: %s, content: %s \n", templateCode, clientId, content);
    if(strcmp(_clientId, clientId)) {
        String fragment;
        String message;

        if (strcmp(templateCode, "518")==0) {
            fragment = "c8y_Relay";
        } else if (strcmp(templateCode, "510")==0) {
            fragment = "c8y_Restart";
        } else if (strcmp(templateCode, "511")==0) {
            fragment = "c8y_Command";
        } else if (strcmp(templateCode, "513")==0) {
            fragment = "c8y_Configuration";
        } else if (strcmp(templateCode, "515")==0) {
            fragment = "c8y_Firmware";
        } else if (strcmp(templateCode, "516")==0) {
            fragment = "c8y_Software";
        } else if (strcmp(templateCode, "519")==0) {
            fragment = "c8y_RelayArray";
        }

        message = "501," + fragment;
        _client.publish("s/us", message.c_str());

        char* fullContentPayload = strstr(payload, content);
        int status = callback(templateCode, fullContentPayload);

        if (status == 0) {
            message = "503," + fragment;
            _client.publish("s/us", message.c_str());
        } else {
            message = "502," + fragment;
            _client.publish("s/us", message.c_str());
        }
    }
}

void CumulocityClient::parseCredentials(char* payload) {
	Serial.println("parseCredentials()");

	char** elements = parseCSV(payload);
	//free(elements[0]);
	_credentials.tenant = elements[1];
	_credentials.username = elements[2];
	_credentials.password = elements[3];
	Serial.println("copied credentials");
	free(elements);
}

char** CumulocityClient::parseCSV(char* payload) {
	char **buf, **bptr, *tmp, *tptr;
    const char *ptr;
    int fieldcnt, fQuote, fEnd;

    fieldcnt = countFields( payload );

    if ( fieldcnt == -1 ) {
        return NULL;
    }

    buf = (char**) malloc( sizeof(char*) * (fieldcnt+1) );

    if ( !buf ) {
        return NULL;
    }

    tmp = (char*) malloc( strlen(payload) + 1 );

    if ( !tmp )
    {
        free( buf );
        return NULL;
    }

    bptr = buf;

    for ( ptr = payload, fQuote = 0, *tmp = '\0', tptr = tmp, fEnd = 0; ; ptr++ )
    {
        if ( fQuote )
        {
            if ( !*ptr ) {
                break;
            }

            if ( *ptr == '\"' )
            {
                if ( ptr[1] == '\"' )
                {
                    *tptr++ = '\"';
                    ptr++;
                    continue;
                }
                fQuote = 0;
            }
            else {
                *tptr++ = *ptr;
            }

            continue;
        }

        switch( *ptr )
        {
            case '\"':
                fQuote = 1;
                continue;
            case '\0':
                fEnd = 1;
            case ',':
                *tptr = '\0';
                *bptr = strdup( tmp );

                if ( !*bptr )
                {
                    for ( bptr--; bptr >= buf; bptr-- ) {
                        free( *bptr );
                    }
                    free( buf );
                    free( tmp );

                    return NULL;
                }

                bptr++;
                tptr = tmp;

                if ( fEnd ) {
                  break;
                } else {
                  continue;
                }

            default:
                *tptr++ = *ptr;
                continue;
        }

        if ( fEnd ) {
            break;
        }
    }

    *bptr = NULL;
    free( tmp );
    return buf;
}

void CumulocityClient::freeCSVElements( char **parsed )
{
    char **ptr;

    for ( ptr = parsed; *ptr; ptr++ ) {
        free( *ptr );
    }

    free( parsed );
}

int CumulocityClient::countFields( const char *line )
{
    const char *ptr;
    int cnt, fQuote;

    for ( cnt = 1, fQuote = 0, ptr = line; *ptr; ptr++ )
    {
        if ( fQuote )
        {
            if ( *ptr == '\"' )
            {
                if ( ptr[1] == '\"' )
                {
                    ptr++;
                    continue;
                }
                fQuote = 0;
            }
            continue;
        }

        switch( *ptr )
        {
            case '\"':
                fQuote = 1;
                continue;
            case ',':
                cnt++;
                continue;
            default:
                continue;
        }
    }

    if ( fQuote ) {
        return -1;
    }

    return cnt;
}
