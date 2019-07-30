#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <time.h>
#include <mosquitto.h>

#define mqttServer "m24.cloudmqtt.com"
#define mqttPort 13876

#define COURT_PASSWD_FILE "/etc/courPwdFile"

#define payloadCheck(A) (strncasecmp(message->payload,A,strlen(A)) == 0)
#define topicCheck(A) (strncasecmp(message->topic,A,strlen(A)) == 0)

char mqttUser[]="uwstvadn";
char mqttPass[]="toyO5JR-vw60";

char subscriptionTopic[]="command/court/control";
char publishingTopic[]="response/court/control";

#define ON 1
#define OFF 0
#define board "192.168.0.90"
#define cred "YWRtaW46YWRtaW4="

char relayMap[] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','-'};

int courtA50[] = {1, 3, 4, 6, 8, 10, -1};
int courtAC50[] = {2, 5, 7, 9, 11, -1};

int courtB50[] = {13, 15, 17, 19, 21, -1};
int courtBC50[] = {12, 14, 16, 18, 20, 22, -1};

int shutterOpenRelay = 25;
int shutterCloseRelay = 24;

int testLights[] = {13, 22, -1};

int initLog()
{
openlog("courtControl", LOG_NDELAY|LOG_PID, LOG_DAEMON);
return 0;
}

int deInitLog()
{
closelog();
return 0;
}

struct TIME
{
  int seconds;
  int minutes;
  int hours;
};

typedef struct pwdStruct {
struct pwdStruct *next;
struct pwdStruct *prev;
char *name;
int pass;
int allowClose; // 0=No, 1=yes
int hour;
int min;
int requestClose;
}pwdStruct;

void operateRelay(int relay, int state)
{
if (checkStatus(relay) != state) {
char cmd[255];
memset(cmd,0,255);
snprintf(cmd, 255, "wget -q -O /tmp/relayOp %s/relays.cgi?relay=%c --header=\"Authorization: Basic %s\"", board, relayMap[relay], cred);
syslog(LOG_CRIT, "%s\n", cmd);
system(cmd);
}
return;
}

void operateRelSet(int *set, int state)
{
getStatus();
int cntr=0;
int relay=set[cntr++];
        while(relay != -1) {
                operateRelay(relay, state);
                relay=set[cntr++];
        }

}

void differenceBetweenTimePeriod(struct TIME start, struct TIME stop, struct TIME *diff)
{
    if(stop.seconds > start.seconds){
        --start.minutes;
        start.seconds += 60;
    }

    diff->seconds = start.seconds - stop.seconds;
    if(stop.minutes > start.minutes){
        --start.hours;
        start.minutes += 60;
    }

    diff->minutes = start.minutes - stop.minutes;
    diff->hours = start.hours - stop.hours;
}

pwdStruct * loadPwdFromStr(char* a_str, const char a_delim)
{
int i=0;
pwdStruct *ret=(pwdStruct *) calloc(1, sizeof(pwdStruct));

  while (a_str[i]) {
    if(a_str[i] == '\r')
      a_str[i]=0;
    if(a_str[i] == '\n')
      a_str[i]=0;
    i++;
  }
char *st=NULL;
char *en=NULL;
i=0;
int ctr=0;
  while(1) {
    en=NULL;
    if(st==NULL)
      st=&a_str[i];
    if((a_str[i] == 0) || (a_str[i] == a_delim))
      en=&a_str[i];
    if(en) {
      char *tmp=strndup(st, (size_t)((unsigned int)en-(unsigned int)st));
      st=NULL;
        if(ctr == 0) {
          ret->name=tmp;
        } else if(ctr == 1) {
          ret->pass=atoi(tmp);
          free(tmp);
          tmp=NULL;
        } else if(ctr == 2) {
          ret->allowClose=ret->requestClose=-1;
          if(tmp[0]=='y')
            ret->allowClose=1;
          else if(tmp[0]=='n')
            ret->allowClose=0;
          else if(tmp[0]=='*')
            ret->requestClose=1;
          else if(tmp[0]=='#')
            ret->requestClose=0;
          free(tmp);
          tmp=NULL;
        } else if(ctr == 3) {
	  if(tmp[0]=='*')
	    ret->hour=-1;
	  else
            ret->hour=atoi(tmp);
          free(tmp);
          tmp=NULL;
        } else if(ctr == 4) {
	  if(tmp[0]=='*')
	    ret->hour=-1;
	  else
          ret->min=atoi(tmp);
          free(tmp);
          tmp=NULL;
        }
        ctr++;
      }
    if(a_str[i] == 0)
      break;
    i++;
  }
return ret;
}

pwdStruct *  loadPwdFromFile(char *fileName)
{
    FILE * fp;
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    char** tokens;
    pwdStruct *head=NULL;
    pwdStruct *tmp=NULL;
    fp = fopen(fileName, "r");
    if (fp == NULL)
        exit(EXIT_FAILURE);

  while ((read = getline(&line, &len, fp)) != -1) {
    tmp = loadPwdFromStr(line, ',');
    if(head==NULL)
      head=tmp;
    else {
      tmp->next=head;
      head->prev=tmp;
      head=tmp;
    }
  }

    fclose(fp);
    if (line)
        free(line);
    return head;
}

int handlePassword(char *password)
{
pwdStruct *pass=NULL;
pwdStruct *tmp=NULL;
int grantAccess=0;
time_t t1;
time(&t1);
struct tm *t=localtime(&t1);
pass=loadPwdFromStr(password,':');
	if (pass) {
	pwdStruct *confList=loadPwdFromFile(COURT_PASSWD_FILE);
	if(pass->requestClose == -1)
	  printf("Incorrect Password\n");
	tmp=confList;
	  while(tmp) {
	    if(pass->pass == tmp->pass) {
		if(tmp->hour == -1) {
		  grantAccess=1;
          	  syslog(LOG_CRIT,"Access Granted to %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
		  break;
		}
		if(pass->requestClose) {
		  if(tmp->allowClose == 0) {
        	    syslog(LOG_CRIT,"Access Denied to close for %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
		    break;
		  } else {
		    grantAccess=1;
        	    syslog(LOG_CRIT,"Access Granted to close for %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
		    break;
		  }
		}
	      struct TIME stTm, spTm, dfTm;
	      stTm.hours=t->tm_hour;
	      stTm.minutes=t->tm_min;
	      stTm.seconds=0;
	      spTm.hours=tmp->hour;
	      spTm.minutes=tmp->min;
	      spTm.seconds=0;
	      differenceBetweenTimePeriod(stTm, spTm, &dfTm);
	      dfTm.minutes=(dfTm.hours*60)+dfTm.minutes;
	      if(dfTm.minutes < 0) {
		if (dfTm.minutes > -16) {
			grantAccess=1;
        		syslog(LOG_CRIT,"Access Granted to %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
			break;
		}
	      } else if(dfTm.minutes < 30) {
			grantAccess=1;
        		syslog(LOG_CRIT,"Access Granted to %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
			break;
	      }
	      if(grantAccess == 0) {
       		syslog(LOG_CRIT,"Access Denied to %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
	      }
	    }
	  tmp=tmp->next;
	  }
	if(grantAccess) {
	  if(pass->requestClose) {
            getStatus();
            operateRelay(shutterCloseRelay, ON);
            getStatus();
            operateRelay(shutterCloseRelay, OFF);
	  } else if(pass->requestClose == 0) {
            getStatus();
            operateRelay(shutterOpenRelay, ON);
            getStatus();
            operateRelay(shutterOpenRelay, OFF);
	  } else {
       		syslog(LOG_CRIT,"Access Denied to %s at %d:%d", tmp->name, t->tm_hour, t->tm_min);
	  }
	}
	}
}


int getStatus()
{
char cmd[255];
memset(cmd,0,255);
snprintf(cmd, 255, "wget -q -O /tmp/status.xml http://%s/status.xml --header=\"Authorization: Basic %s\"", board, cred);
syslog(LOG_CRIT, "%s\n", cmd);
system(cmd);
}

int checkStatus(int relay)
{
char cmd[255];
memset(cmd,0,255);
snprintf(cmd, 255, "grep -q \"<relay%d>1\" /tmp/status.xml", relay);
int ret=system(cmd);
return (ret?0:1);
return 1;
}

void publishStatus(struct mosquitto *mosq)
{
getStatus();
int i=0;
char pubVal[33];
for(i=0;i<32;i++) {
        if(checkStatus(i))
                pubVal[i]='1';
        else
                pubVal[i]='0';
}
pubVal[i++]='-';
pubVal[i]=0;
//mosquitto_publish(mosq, NULL, publishingTopic, 255, lastCommand, 2, 0);
mosquitto_publish(mosq, NULL, publishingTopic, (int)strlen(pubVal), pubVal, 2, 0);
}

int checkCourtA()
{
getStatus();
int cntr=0;
int relay=courtA50[cntr++];
int ret=0;
        while(relay != -1) {
                ret+=checkStatus(relay);
                relay=courtA50[cntr++];
        }
cntr=0;
relay=courtAC50[cntr++];
        while(relay != -1) {
                ret+=checkStatus(relay);
                relay=courtAC50[cntr++];
        }
int totRel=(int)((sizeof(courtA50) + sizeof(courtAC50))/sizeof(int))-2;
int percent=(ret*100)/totRel;
return percent;
}

int checkTestLights()
{
getStatus();
int cntr=0;
int relay=testLights[cntr++];
int ret=0;
        while(relay != -1) {
                ret+=checkStatus(relay);
                relay=testLights[cntr++];
        }
int totRel=(int)(sizeof(testLights));
int percent=(ret*100)/totRel;
return percent;
}

int checkCourtB()
{
getStatus();
int cntr=0;
int relay=courtB50[cntr++];
int ret=0;
        while(relay != -1) {
                ret+=checkStatus(relay);
                relay=courtB50[cntr++];
        }
cntr=0;
relay=courtBC50[cntr++];
        while(relay != -1) {
                ret+=checkStatus(relay);
                relay=courtBC50[cntr++];
        }
int totRel=(int)((sizeof(courtB50) + sizeof(courtBC50))/sizeof(int))-2;
int percent=(ret*100)/totRel;
return percent;
}

void my_connect_callback(struct mosquitto *mosq, void *usd, int result)
{
	syslog(LOG_CRIT, "%s called \n", __FUNCTION__);
	mosquitto_subscribe(mosq, NULL, subscriptionTopic, 0);
}

void my_subscribe_callback(struct mosquitto *mosq, void *userdata, int mid, int qos_count, const int *granted_qos)
{
int i;
	syslog(LOG_CRIT, "%s called \n", __FUNCTION__);
        syslog(LOG_CRIT,"Subscribed (mid: %d): %d", mid, granted_qos[0]);
        for(i=1; i<qos_count; i++){
                syslog(LOG_CRIT,", %d", granted_qos[i]);
        }

}

void my_disconnect_callback(struct mosquitto *mosq, void *userdata, int disconnect_int)
{
	syslog(LOG_CRIT, "%s called disconnect_int: %d\n", __FUNCTION__, disconnect_int);
}

void my_message_callback(struct mosquitto *mosq, void *usd, const struct mosquitto_message *message)
{
int handled=1;
char publish[255];
	syslog(LOG_CRIT, "%s called topic: %s, payload: %s\n", __FUNCTION__, message->topic, message->payload);
#if 1
        if(message->payloadlen) {
                if(topicCheck(subscriptionTopic)) {
                        if(payloadCheck("testLightsON")) {
                                operateRelSet(testLights, ON);
                                operateRelSet(testLights, ON);
                        } else if(payloadCheck("testLightsOFF")) {
                                operateRelSet(testLights, OFF);
                                operateRelSet(testLights, OFF);
                        } else if(payloadCheck("courtAON")) {
                                operateRelSet(courtA50, ON);
                                operateRelSet(courtAC50, ON);
                        } else if(payloadCheck("courtAOFF")) {
                                operateRelSet(courtA50, OFF);
                                operateRelSet(courtAC50, OFF);
                        } else if(payloadCheck("courtA50ON")) {
                                operateRelSet(courtA50, ON);
                        } else if(payloadCheck("courtA50OFF")) {
                                operateRelSet(courtA50, OFF);
                        } else if(payloadCheck("courtAC50ON")) {
                                operateRelSet(courtAC50, ON);
                        } else if(payloadCheck("courtAC50OFF")) {
                                operateRelSet(courtAC50, OFF);
                        } else if(payloadCheck("courtBON")) {
                                //operateRelSet(courtB50, ON);
                                //operateRelSet(courtBC50, ON);
                        } else if(payloadCheck("courtBOFF")) {
                                operateRelSet(courtB50, OFF);
                                operateRelSet(courtBC50, OFF);
                        } else if(payloadCheck("courtB50ON")) {
                                operateRelSet(courtB50, ON);
                        } else if(payloadCheck("courtB50OFF")) {
                                operateRelSet(courtB50, OFF);
                        } else if(payloadCheck("courtBC50ON")) {
                                operateRelSet(courtBC50, ON);
                        } else if(payloadCheck("courtBC50OFF")) {
                                operateRelSet(courtBC50, OFF);
                        } else if(payloadCheck("shutterOpen")) {
                                getStatus();
                                operateRelay(shutterOpenRelay, ON);
                                getStatus();
                                operateRelay(shutterOpenRelay, OFF);
                        } else if(payloadCheck("shutterClose")) {
                                getStatus();
                                operateRelay(shutterCloseRelay, ON);
                                getStatus();
                                operateRelay(shutterCloseRelay, OFF);
                        } else if(payloadCheck("Password:")) {
                                syslog(LOG_CRIT, "Password Handling %s\n", message->payload);
				handlePassword(message->payload);
                        } else if(payloadCheck("getStatus")) {
                                publishStatus(mosq);
                        } else {
                                syslog(LOG_CRIT, "unhandled message %s\n", message->payload);
				handled=0;
                        }
                } else {
                        syslog(LOG_CRIT, "unhandled topic %s\n", message->topic);
			handled=0;
		}
        }
	if(handled) {
		memset(publish, 0, 255);
		snprintf(publish, 255, "%s handled", message->payload);
		mosquitto_publish(mosq, NULL, publishingTopic, 255, publish, 2, 0);
	}
#endif
return;
}


int initMosquitto(struct mosquitto **mosq)
{
bool cleanSession=true;
int mosquittoRet;

mosquitto_lib_init();
*mosq=mosquitto_new("BalajiSession", cleanSession, NULL);

if (*mosq == NULL) {
	syslog(LOG_CRIT, "mosquitto_new failure mosqerrno : %d %s\n",
		mosquittoRet, (MOSQ_ERR_ERRNO == mosquittoRet)?strerror(errno):"mosq failure, Not system failure");
	return -1;
}

mosquittoRet= mosquitto_username_pw_set(*mosq, mqttUser, mqttPass);
if(MOSQ_ERR_SUCCESS != mosquittoRet) {
	syslog(LOG_CRIT, "mosquitto_username_pw_set failure mosqerrno : %d %s\n",
		mosquittoRet, (MOSQ_ERR_ERRNO == mosquittoRet)?strerror(errno):"mosq failure, Not system failure");
	return -1;
}

mosquitto_connect_callback_set(*mosq, my_connect_callback);
mosquitto_message_callback_set(*mosq, my_message_callback);
mosquitto_subscribe_callback_set(*mosq, my_subscribe_callback);
mosquitto_disconnect_callback_set(*mosq, my_disconnect_callback);


return 0;
}

int deInitMosquitto(struct mosquitto **mosq)
{
mosquitto_destroy(*mosq);
mosquitto_lib_cleanup();
}

int main(int argc, char *argv[])
{
int keepalive=10;
struct mosquitto *mosq = NULL;
int mosquittoRet;

int i;

initLog();
initMosquitto(&mosq);

mosquittoRet=MOSQ_ERR_CONN_PENDING;
do {
mosquittoRet=mosquitto_connect(mosq, mqttServer, mqttPort, keepalive);

if(MOSQ_ERR_SUCCESS != mosquittoRet) {
	syslog(LOG_CRIT, "mosquitto_connect failure mosqerrno : %d %s\n",
		mosquittoRet, (MOSQ_ERR_ERRNO == mosquittoRet)?strerror(errno):"mosq failure, Not system failure");
	sleep(2);
}

} while(mosquittoRet != MOSQ_ERR_SUCCESS);

mosquittoRet=mosquitto_loop_forever(mosq, -1, 1);

if(MOSQ_ERR_SUCCESS != mosquittoRet) {
	syslog(LOG_CRIT, "mosquitto_connect failure mosqerrno : %d %s\n",
		mosquittoRet, (MOSQ_ERR_ERRNO == mosquittoRet)?strerror(errno):"mosq failure, Not system failure");
}

deInitMosquitto(&mosq);
deInitLog();
}
