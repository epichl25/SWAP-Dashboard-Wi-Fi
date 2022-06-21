#include <Arduino.h>  //Import Arduino Core libraries
#include <ESP8266WiFi.h>  //Import esp8266 library
#include <String.h>  //Import String library
#include <PubSubClient.h>   //Import MQTT client
#include <NTPClient.h>  //Import NTP server (Get time and date)
#include <WiFiUdp.h>  
/*
Hardware Serial is for comm with master device
Hardware Serial1 for debugging: D4
*/

/*
***************************************
  Wifi
***************************************
*/
//Wifi
String WIFI_SSID;
String WIFI_PWD;
IPAddress Wifi_IP;
WiFiClient espClient;
/*
***************************************
  MQTT 
***************************************
*/
// MQTT Broker
String mqtt_broker_str;
String mqtt_username_str;
String mqtt_password_str;
String mqtt_port_str;
String device_ID_str;

//Note: Need to reset esp8266 Wifi module to change the MQTT credentials
const char *mqtt_broker;  // server IP
const char *mqtt_username;  //authentication credentials
const char *mqtt_password;  //authentication credentials
int mqtt_port;  //port
const char *device_ID;  //Device name <= mqtt topic

//Global Variables
String cmd_str;  //Command string
String recv_data;  //Received data string
String server_data;  //Server data

PubSubClient client(espClient);

/*
***************************************
  Network Time Protocol 
***************************************
*/
// Server map:https://www.ntppool.org/en/
// By default 'pool.ntp.org' is used with 60 seconds update interval and

//HK time in UTC +8.00: 8*60*60=28800
const long utcOffsetInSeconds = 28800;  //Time region
int chnge_server=0;
char *NTP_server="0.hk.pool.ntp.org";  //Server to connect to 
//Month names
String months[12]={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP,NTP_server, utcOffsetInSeconds);
time_t epochTime;
String formattedTime;
String currentDate;
String currentMonthName;
time_t old_epochTime;
int server_rcnt_cnt=0;
int NTP_fail_flag=0;

//Connect to Wifi=>MQTT=>NTP
void setup() 
{
  //Config COM UART
  Serial.begin(115200); 
  Serial.flush();
  //Config debug UART
  Serial1.begin(74880);  
  Serial1.flush();
  Serial1.println("UART configs complete");
  delay(10);
  
  //Connect to WIFI and MQTT server
  Serial1.println("Waiting for intial CMD");
  while (cmd_str==NULL)  //wait for initial command
  {
   
    UART_master_recv();  //Receive data via UART
  }
  if (cmd_str=="JN_WIFI")
  {
    Serial1.println("cmd_str:'JN_WIFI' received from master");

    //recv_data: <WIFI_SSD;WIFI_PWD;mqtt_username;mqtt_password;mqtt_port;device;ID>
    //Format received data to WIFI credentials
    //Find indexes
    int fd_semicln_index1 = recv_data.indexOf(';');  
    int fd_semicln_index2 = recv_data.indexOf(';',fd_semicln_index1+1);
    int fd_semicln_index3 = recv_data.indexOf(';',fd_semicln_index2+1);
    int fd_semicln_index4 = recv_data.indexOf(';',fd_semicln_index3+1);
    int fd_semicln_index5 = recv_data.indexOf(';',fd_semicln_index4+1);
    int fd_semicln_index6 = recv_data.indexOf(';',fd_semicln_index5+1);

    //Extract data
    WIFI_SSID=recv_data.substring(0, fd_semicln_index1);  
    WIFI_PWD=recv_data.substring(fd_semicln_index1+1, fd_semicln_index2);

    mqtt_broker_str=recv_data.substring(fd_semicln_index2+1, fd_semicln_index3);
    mqtt_broker=mqtt_broker_str.c_str();

    mqtt_username_str=recv_data.substring(fd_semicln_index3+1, fd_semicln_index4);
    mqtt_username=mqtt_username_str.c_str();

    mqtt_password_str=recv_data.substring(fd_semicln_index4+1, fd_semicln_index5);
    mqtt_password=mqtt_password_str.c_str();

    mqtt_port_str=recv_data.substring(fd_semicln_index5+1,fd_semicln_index6);  
    mqtt_port=mqtt_port_str.toInt();  //Convert port no. from str to int
    
    device_ID_str=recv_data.substring(fd_semicln_index6+1, -1);
    device_ID=device_ID_str.c_str();
    
    //Debug text
    Serial1.println("");
    Serial1.println("****************************************************");
    Serial1.println("RECV_DATA FROM MASTER");
    Serial1.println("****************************************************");
    Serial1.print("WIFI SSD:");
    Serial1.print(WIFI_SSID);
    Serial1.println(":");
    Serial1.print("WIFI PWD:");
    Serial1.print(WIFI_PWD);
    Serial1.println(":");
    Serial1.print("mqtt_broker:");
    Serial1.print(mqtt_broker);
    Serial1.println(":");
    Serial1.print("mqtt_username:");
    Serial1.print(mqtt_username);
    Serial1.println(":");
    Serial1.print("mqtt_password:");
    Serial1.print(mqtt_password);
    Serial1.println(":");
    Serial1.print("mqtt_port:");
    Serial1.print(mqtt_port);
    Serial1.println(":");
    Serial1.print("device_ID:");
    Serial1.print(device_ID);
    Serial1.println(":");
    
    connect();  // Connect to Wi-Fi, MQTT, NTP
    client.publish(device_ID,"<DASHBOARD_CONNECTED>");

    //Send ack to master -- <Connected=time;date>
    String initAck;
    initAck+="<CONNECTED=";
    initAck+=Wifi_IP.toString();
    initAck+=",";
    initAck+=formattedTime;
    initAck+=";";
    initAck+=currentDate;
    initAck+=">";
    Serial.print(initAck);
    
  }
}

/**********************
  MAIN 
 *********************/
void loop() //<=need to check for time&date
{
  client.loop(); //Keep connection alive

  if (WiFi.status()!=WL_CONNECTED || !client.connected() ) //Check if Wi-Fi & MQTT are connected
  {
    if (WiFi.status()!=WL_CONNECTED)
    {
      Serial1.println("WiFi disconnected");
    }
    else if (!client.connected())
    {
      Serial1.println("MQTT disconnected");
    }
    //Ensure disconnected
    client.disconnect();  //Disconnect from MQTT server
    WiFi.disconnect();  //Disconnect Wi-Fi

    connect();  //Reconnect 
    client.publish(device_ID,"<DASHBOARD_RECONNECTED>");
  }
  delay(100);
}

/*********************
 * Server reply func
*********************/
//Callback loop checks for data from server
void callback(char *topic, byte *payload, unsigned int length) 
{
  Serial1.print("Message arrived in topic: ");
  Serial1.println(topic);
  Serial1.print("Message:");
  server_data="";  //Flush string
  if(strcmp(topic, device_ID) == 0)  //If topic is equal to the device's subscribed topic
  {
    for (int i = 0; i < length; i++) //Get data from server
    {
      server_data+=(char )payload[i];  //Add chars to string
    }
    Serial1.println(server_data);
    //Check if cmd from server is valid
    int fd_cmd_index2 = server_data.indexOf('=');  //Find index of '='
    String chk_cmd=server_data.substring(1, fd_cmd_index2);  //Extract cmd
    if (chk_cmd=="RECV_DAI" || chk_cmd=="RECV_MNT")
    {
      //Send server data to master
      Serial.print(server_data);
    }
  }
}

/**********************************
  UART funcs
 *********************************/
void UART_master_recv()  //Blocking
{
  //Flush strings
  String str="";  
  String CMDstr="";
  if (Serial.available()>0)  //Check if there is any data
  {
    Serial1.println("UART receiving...");
    char uart_recv = Serial.read();  
    if (uart_recv=='<')
    {
      uart_recv = Serial.read();
      while(uart_recv!='>')
      {
        str=str+uart_recv;
        uart_recv=Serial.read();
      }
      Serial1.println("");
      Serial1.println("****************************************************");
      Serial1.println("UART FROM MASTER");
      Serial1.println("****************************************************");
      Serial1.print("String received:");
      Serial1.print(str);
      Serial1.println(":");
    }
    if(str.indexOf('=') > 0)
    {
      int fd_cmd_index = str.indexOf('=');  //Find index of '='
      cmd_str=str.substring(0, fd_cmd_index);  //Extract cmd
      recv_data=str.substring(fd_cmd_index+1,-1);  //Extract data
      Serial1.print("CMD str=:");
      Serial1.print(cmd_str);
      Serial1.println(":");
      Serial1.print("recv_data=:");
      Serial1.print(recv_data);
      Serial1.println(":");
      Serial1.println("");
    }
    else
    {
      CMDstr=str.substring(0,-1);
      Serial1.print("CMDstr=:");
      Serial1.print(CMDstr);
      Serial1.println(":");
      Serial1.println("");
    } 
    
  }
  delay(50);
}


/*************************
 Wifi funcs
 ************************/
int WIFI_stat_chk()  //Checks WIFI connection status
{
  if (WiFi.status()!=WL_CONNECTED)  
  {
    return 0;
  }
  else
  {
    return 1;
  }
}

void connect(void)
{
  //Connect to WIFI
    int cnt_rqst=0;
    WiFi.setPhyMode(WIFI_PHY_MODE_11G); //Set radio type
    WiFi.mode(WIFI_STA);  
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    
    //The ESP8266 tries to reconnect automatically when the connection is lost
    WiFi.setAutoReconnect(true);

    Serial1.println("");
    Serial1.println("****************************************************");
    Serial1.println("INTIALISING WIFI");
    Serial1.println("****************************************************");
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial1.println("Connecting to Wifi");
      if (cnt_rqst>60)  //If it fails to connect after 60 requests
      {
        Serial.print("<FAILED=WIFI>"); //Send cmd to master to exit 
        Serial1.println("WIFI failed");  //Debug
        Serial1.println("Wifi failed - restart me please");  
        while (1)  //Enter infinite loop to prevent code from running
        {
          
        }
      }
      cnt_rqst++;
      delay(500);
      Serial1.print("cnt_rqst:");
      Serial1.println(cnt_rqst);
    }
    Wifi_IP=WiFi.localIP(); //Get local IP

    Serial1.println("");
    Serial1.println("****************************************************");
    Serial1.println("INTIALISING MQTT");
    Serial1.println("****************************************************");
    //Connect to a mqtt broker
    int cnt_mqttcont=0;
    client.setServer(mqtt_broker, mqtt_port);  //Connect to server
    client.setCallback(callback);  
    client.setBufferSize(65535);  // Set Maximum packet size
    client.setKeepAlive (65535);  // Set keepAlive interval in Seconds
    client.setSocketTimeout (15);  //Set socket timeout interval in Seconds
    while (!client.connected())
    {
      String client_id = device_ID_str;
      client_id+='-';
      client_id += String(WiFi.macAddress());
      Serial1.println("The client connecting to the mqtt broker");
      cnt_mqttcont++;
      //Use authentication if necessary
      if (client.connect(client_id.c_str(), mqtt_username, mqtt_password))  //if connected to server and wifi
      {
        Serial1.println("MQTT broker connected");
      }
      else if (cnt_mqttcont>30) //retry 30 times
      {
        Serial.print("<FAILED=MQTT>");  //Send cmd to master to exit 
        Serial1.println("MQTT failed after 30 tries");  
        Serial1.println("MQTT failed - restart me please");  
        while (1)  //Enter infinite loop to prevent code from running
        {
        }
      }
      else
      {
        Serial1.print("failed with state ");
        Serial1.println(client.state());
        delay(500);
      }
    }

    Serial1.println("");
    Serial1.println("****************************************************");
    Serial1.println("INTIALISING NTP");
    Serial1.println("****************************************************");
    //Connect to NTP server
    NTP_gt(); 
    if (NTP_fail_flag==1)  
    {
      Serial.print("<FAILED=NTP>");  //Send cmd to master to exit 
      Serial1.println("NTP failed -- Entering infinite loop...");  
      NTP_fail_flag=0;
      Serial1.println("NTP failed - restart me please");  
      while (1)  //Enter infinite loop to prevent code from running
      {
      }
    }

  //Subscribe to device's topic
  Serial1.println("");
  Serial1.println("****************************************************");
  Serial1.println("SUBSCRIBING TO TOPIC");
  Serial1.println("****************************************************"); 
  client.subscribe(device_ID);
}

/*************************
 NTP funcs
 ************************/
void NTP_servers()  //Changes NTP server
{
  switch(chnge_server)  //Server selection pool
  {
    case 1:
      NTP_server="1.hk.pool.ntp.org";
      chnge_server++;
      break;
    case 2:
      NTP_server="2.hk.pool.ntp.org";
      chnge_server++;
      break;
    case 3:
      NTP_server="3.hk.pool.ntp.org";
      chnge_server=0;
    default:
      NTP_server="0.hk.pool.ntp.org";
      chnge_server++;
      break;
  }
}

//Gets data from NTP servers and checks NTP server connection
void NTP_gt()  //Get NTP data
{
  old_epochTime=epochTime;  
  timeClient.begin();  // Connect to server again
  timeClient.update();  //Update 
  epochTime = timeClient.getEpochTime();  //Get time
  while (epochTime==old_epochTime || epochTime==NULL)  //If there is no update from the server 
  {
    int retry_NTP_cnt=0;
    while (retry_NTP_cnt<20)  //Retry 20 times per server
    {
      old_epochTime=epochTime;  //Save old epochTime

      timeClient.begin();  // Connect to server again
      timeClient.update();  //Update 

      //Get time
      epochTime = timeClient.getEpochTime();
      if (epochTime==old_epochTime || epochTime==NULL)
      {
        retry_NTP_cnt++;  //Retry count
      }
      else
      {
        break;  //Successfully connected - exit infinite loop
      }
    }
    if (epochTime==old_epochTime || epochTime==NULL)  //if previous attempt failed
    {
      NTP_servers();  // Change the server
      server_rcnt_cnt++;  //Server change count
      if (server_rcnt_cnt==3)  //Check if all available servers have failed to connect
      {
        NTP_fail_flag=1;
        break;  //Failed to connect - exit both infinite loops with failed flag
      }
    }
    else
    {
      break;  //Successfully connected - exit both infinite loops
    }
  }

  if (NTP_fail_flag!=1) //Get NTP data
  {
    //Get time
    formattedTime = timeClient.getFormattedTime();
    //Get a time structure
    struct tm *ptm = gmtime ((time_t *)&epochTime); 
    //Get date
    int monthDay = ptm->tm_mday;
    int currentMonth = ptm->tm_mon+1;
    currentMonthName = months[currentMonth-1];
    int currentYear = ptm->tm_year+1900;
    currentDate = String(currentYear) + "/" + String(currentMonth) + "/" + String(monthDay);
  }
}
