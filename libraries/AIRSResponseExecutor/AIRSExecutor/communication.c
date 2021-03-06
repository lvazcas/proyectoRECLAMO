/* communication.c
 *
 * Copyright (c) 2005-2008 Frank Knobbe <frank@knobbe.us>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * 
 *
 * Purpose:
 *
 * This tool sends execute-action/undo-action requests to a remote host running ExecutorAgent
 * (the agent) which will execute-action/undo-action the intruding IP address on a variety of
 * host and network firewalls.
 * The communication over the network is encrypted using two-fish.
 * (Implementation ripped from CryptCat by Farm9 with permission.)
 *
 *
 * Comments:
 * 
 * Several modifications were made for Danny Guamán <ds.guaman@alumnos.upm.es, danny.guamanl@gmail.com> 2012
 * It tool was used to build a module part of Based Ontologies Automated Intrusion Detection System. It's part of a project research
 * at Techical University of Madrid.
 *
*/
#ifndef		__COMMUNICATION_C__
#define		__COMMUNICATION_C__
#include "communication.h"

		
void waitms(unsigned int dur)
{
#ifdef WIN32
        Sleep(dur);
#else
        usleep(dur*1000);
#endif
}

/*      This function (together with the define in airsexecutor.h) attempts
 *      to prevent buffer overflows by checking the destination buffer size.
*/
void _safecp(char *dst,unsigned long max,char *src)
{	if(dst && src && max)
	{	while(--max>0 && *src)
			*dst++ = *src++;
		*dst=0;
	}
}

/*  Generates a new encryption key for TwoFish based on seq numbers and a random that
 *  the Executor Agents send on checkin (in protocol)
*/
void GenerateNewCipherKey(ExecutorAgent *agent, ActionRequestPacket *packet)
{	unsigned char newkey[TwoFish_KEY_LENGTH+2];
	int i;

	newkey[0]=packet->airsSeqNo[0];		/* current AIRSexecutor seq # (which both know) */
	newkey[1]=packet->airsSeqNo[1];			
	newkey[2]=packet->agentSeqNo[0];			/* current ExecutorAgent seq # (which both know) */
	newkey[3]=packet->agentSeqNo[1];
	newkey[4]=packet->protocol[0];		/* the random ExecutorAgent chose */
	newkey[5]=packet->protocol[1];

	strncpy(newkey+6, agent->agentKey ,TwoFish_KEY_LENGTH-6); /* append old key */
	newkey[TwoFish_KEY_LENGTH]=0;

	newkey[0]^=agent->myKeyMod[0];		/* modify key with key modifiers which were */
	newkey[1]^=agent->myKeyMod[1];		/* exchanged during the check-in handshake. */
	newkey[2]^=agent->myKeyMod[2];
	newkey[3]^=agent->myKeyMod[3];
	newkey[4]^=agent->agentKeyMod[0];
	newkey[5]^=agent->agentKeyMod[1];
	newkey[6]^=agent->agentKeyMod[2];
	newkey[7]^=agent->agentKeyMod[3];

	for(i=0;i<=7;i++)
		if(newkey[i]==0)
			newkey[i]++;

	safecopy(agent->agentKey, newkey);
	TwoFishDestroy(agent->agentFish);
	agent->agentFish=TwoFishInit(newkey);
}


/*  CheckOut will be called when airsExecutor exists. It de-registeres this tool 
 *  from the list of 'AIRS Executors' that the 'Executor Agent' keeps. 
*/
void CheckOut(ExecutorAgent *agent)
{	ActionRequestPacket actionRequest;
	int i,len;
	char *encbuf,*decbuf;


	if(!agent->persistentSocket)
	{	agent->agentSocket=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP); 
		if(agent->agentSocket==INVALID_SOCKET)
		{	fprintf(stderr,"Error: [CheckOut] Invalid Socket error!\n");
			return;
		}
		if(bind(agent->agentSocket,(struct sockaddr *)&(agent->localSocketAddr),sizeof(struct sockaddr)))
		{	fprintf(stderr,"Error: [CheckOut] Can not bind socket!\n");
			return;
		}
		/* let's connect to the agent*/
		i=!connect(agent->agentSocket,(struct sockaddr *)&agent->stationSocketAddr,sizeof(struct sockaddr));
	}
	else
		i=TRUE;
	if(i)
	{	if(verbose>0)
			printf("Info: [CheckOut] Disconnecting from host %s.\n",inet_ntoa(agent->agentIP));
		/* now build the packet */
		agent->mySeqNo+=agent->agentSeqNo; /* increase my seqno */
		actionRequest.endianCheck=1;
		actionRequest.airsSeqNo[0]=(char)agent->mySeqNo;
		actionRequest.airsSeqNo[1]=(char)(agent->mySeqNo>>8);
		actionRequest.agentSeqNo[0]=(char)agent->agentSeqNo; /* fill agentseqno */
		actionRequest.agentSeqNo[1]=(char)(agent->agentSeqNo>>8);
		actionRequest.status=FWSAM_STATUS_CHECKOUT;  /* checking out... */
		actionRequest.version=agent->packetVersion;

		if(verbose>1)
		{	printf("Debug: [CheckOut] Sending CHECKOUT\n");
			printf("Debug: [CheckOut] AIRSExecutor SeqNo:  %x\n",agent->mySeqNo);
			printf("Debug: [CheckOut] ExecutorAgent SeqNo :  %x\n",agent->agentSeqNo);
			printf("Debug: [CheckOut] Status     :  %i\n",actionRequest.status);
		}

		encbuf=TwoFishAlloc(sizeof(ActionRequestPacket),FALSE,FALSE,agent->agentFish); /* get encryption buffer */
		len=TwoFishEncrypt((char *)&actionRequest,(char **)&encbuf,sizeof(ActionRequestPacket),FALSE,agent->agentFish); /* encrypt packet with current key */

		if(send(agent->agentSocket,encbuf,len,0)==len)
		{	i=FWSAM_NETWAIT;
			ioctlsocket(agent->agentSocket,FIONBIO,&i);	/* set non executing and wait for  */
			while(i-- >1)
			{	waitms(10);				/* ...wait a maximum of 3 secs for response... */
				if(recv(agent->agentSocket,encbuf,len,0)==len) /* ... for the status packet */
					i=0;
			}
			if(i) /* if we got the packet */
			{	decbuf=(char *)&actionRequest;
				len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent->agentFish);

				if(len!=sizeof(ActionRequestPacket)) /* invalid decryption */
				{	safecopy(agent->agentKey ,agent->initialKey); /* try initial key */
					TwoFishDestroy(agent->agentFish);			 /* toss this fish */
					agent->agentFish=TwoFishInit(agent->agentKey ); /* re-initialze TwoFish with initial key */
					len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent->agentFish); /* and try to decrypt again */
					if(verbose>1)
						printf("Debug: [CheckOut] Had to use initial key!\n");
				}
				if(len==sizeof(ActionRequestPacket)) /* valid decryption */
				{	if(actionRequest.version!=agent->packetVersion) /* but don't really care since we are on the way out */
						fprintf(stderr,"Error: [CheckOut] Protocol version error!\n");
				}
				else
					fprintf(stderr,"Error: [CheckOut] Password mismatch!\n");
			}
		}
		free(encbuf); /* release TwoFishAlloc'ed buffer */
	}
	else
		fprintf(stderr,"Error: [CheckOut] Could not connect to host %s for CheckOut. What the hell, we're quitting anyway! :)\n",inet_ntoa(agent->agentIP));

	closesocket(agent->agentSocket);
	agent->persistentSocket=FALSE;
}


/*  This routine registers this tool with ExecutorAgent.
 *  It will also change the encryption key based on some variables.
*/
int CheckIn(ExecutorAgent *agent)
{	int i,len,stationok=FALSE,again;
	ActionRequestPacket actionRequest;
	char *encbuf,*decbuf;

	do
	{	again=FALSE;
		/* create a socket for the agent */
		agent->agentSocket=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP); 
		if(agent->agentSocket==INVALID_SOCKET)
		{	fprintf(stderr,"Error: [CheckIn] Invalid Socket error!\n");
			return FALSE;
		}
		if(bind(agent->agentSocket,(struct sockaddr *)&(agent->localSocketAddr),sizeof(struct sockaddr)))
		{	fprintf(stderr,"Error: [CheckIn] Can not bind socket!\n");
			return FALSE;
		}

		/* let's connect to the agent*/
		if(connect(agent->agentSocket,(struct sockaddr *)&agent->stationSocketAddr,sizeof(struct sockaddr)))
		{	fprintf(stderr,"Error: [CheckIn] Could not connect to host %s.\n",inet_ntoa(agent->agentIP));
			return FALSE;
		}
		else
		{	if(verbose>0)
				printf("Info: [CheckIn] Connected to host %s.\n",inet_ntoa(agent->agentIP));
			/* now build the packet */
			actionRequest.endianCheck=1;
			actionRequest.airsSeqNo[0]=(char)agent->mySeqNo; /* fill my sequence number number */
			actionRequest.airsSeqNo[1]=(char)(agent->mySeqNo>>8); /* fill my sequence number number */
			actionRequest.status=FWSAM_STATUS_CHECKIN; /* let's check in */
			actionRequest.version=agent->packetVersion; /* set the packet version */
			memcpy(actionRequest.duration,agent->myKeyMod,4);  /* we'll send ExecutorAgent our key modifier in the duration slot */
												   /* (the checkin packet is just the plain initial key) */
			if(verbose>1)
			{	printf("Debug: [CheckIn] Sending CHECKIN\n");
				printf("Debug: [CheckIn] AIRSExecutor SeqNo:  %x\n",agent->mySeqNo);
				printf("Debug: [CheckIn] Mode       :  %i\n",actionRequest.status);
				printf("Debug: [CheckIn] Version    :  %i\n",actionRequest.version);
			}

			encbuf=TwoFishAlloc(sizeof(ActionRequestPacket),FALSE,FALSE,agent->agentFish); /* get buffer for encryption */
			len=TwoFishEncrypt((char *)&actionRequest,(char **)&encbuf,sizeof(ActionRequestPacket),FALSE,agent->agentFish); /* encrypt with initial key */
			if(send(agent->agentSocket,encbuf,len,0)!=len) /* weird...could not send */
				fprintf(stderr,"Error: [CheckIn] Could not send to host %s.\n",inet_ntoa(agent->agentIP));
			else
			{	i=FWSAM_NETWAIT;
				ioctlsocket(agent->agentSocket,FIONBIO,&i);	/* set non executing action and wait for  */
				while(i-- >1)
				{	waitms(10); /* wait a maximum of 3 secs for response */
					if(recv(agent->agentSocket,encbuf,len,0)==len)
						i=0;
				}
				if(!i) /* time up? */
					fprintf(stderr,"Error: [CheckIn] Did not receive response from host %s.\n",inet_ntoa(agent->agentIP));
				else
				{	decbuf=(char *)&actionRequest; /* got status packet */
					len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent->agentFish); /* try to decrypt with initial key */
					if(len==sizeof(ActionRequestPacket)) /* valid decryption */
					{	if(verbose>1)
						{
							printf("Debug: [CheckIn] Received %s\n",actionRequest.status==FWSAM_STATUS_OK?"OK":
																	   actionRequest.status==FWSAM_STATUS_NEWKEY?"NEWKEY":
																	   actionRequest.status==FWSAM_STATUS_RESYNC?"RESYNC":
																	   actionRequest.status==FWSAM_STATUS_HOLD?"HOLD":"ERROR");
							printf("Debug: [CheckIn] AIRSExecutor SeqNo:  %x\n",actionRequest.airsSeqNo[0]|(actionRequest.airsSeqNo[1]<<8));
							printf("Debug: [CheckIn] ExecutorAgent SeqNo :  %x\n",actionRequest.agentSeqNo[0]|(actionRequest.agentSeqNo[1]<<8));
							printf("Debug: [CheckIn] Status     :  %i\n",actionRequest.status);
							printf("Debug: [CheckIn] Version    :  %i\n",actionRequest.version);
						}

						if(actionRequest.version==FWSAM_PACKETVERSION_PERSISTENT_CONN || actionRequest.version==FWSAM_PACKETVERSION) /* master speaks my language */
						{	if(actionRequest.status==FWSAM_STATUS_OK || actionRequest.status==FWSAM_STATUS_NEWKEY || actionRequest.status==FWSAM_STATUS_RESYNC) 
							{	agent->agentSeqNo=actionRequest.agentSeqNo[0]|(actionRequest.agentSeqNo[1]<<8); /* get stations seqno */
								agent->lastContact=(unsigned long)time(NULL);
								stationok=TRUE;
								agent->packetVersion=actionRequest.version;
								if(actionRequest.version==FWSAM_PACKETVERSION)
									agent->persistentSocket=FALSE;
								
								if(actionRequest.status==FWSAM_STATUS_NEWKEY || actionRequest.status==FWSAM_STATUS_RESYNC)	/* generate new keys */
								{	memcpy(agent->agentKeyMod,actionRequest.duration,4); /* note the key modifier */
									GenerateNewCipherKey(agent,&actionRequest); /* and generate new TwoFish keys (with key modifiers) */
									if(verbose>1)
										printf("Debug: [CheckIn] Generated new encryption key...\n");
								}
							}
							else if(actionRequest.status==FWSAM_STATUS_ERROR && actionRequest.version==FWSAM_PACKETVERSION) 
							{	if(agent->persistentSocket)
								{	fprintf(stderr,"Info: [CheckIn] Host %s doesn't support packet version %i for persistent connections. Trying packet version %i.\n",inet_ntoa(agent->agentIP),FWSAM_PACKETVERSION_PERSISTENT_CONN,FWSAM_PACKETVERSION);
									agent->persistentSocket=FALSE;
									agent->packetVersion=FWSAM_PACKETVERSION;
									again=TRUE;
								}
								else
									fprintf(stderr,"Error: [CheckIn] Protocol version mismatch! Ignoring host %s!\n",inet_ntoa(agent->agentIP));
							}
							else /* weird, got a strange status back */
								fprintf(stderr,"Error: [CheckIn] Funky handshake error! Ignoring host %s!\n",inet_ntoa(agent->agentIP));
						}
						else /* packet version does not match */
							fprintf(stderr,"Error: [CheckIn] Protocol version error! Ignoring host %s!\n",inet_ntoa(agent->agentIP));
					}
					else /* key does not match */
						fprintf(stderr,"Error: [CheckIn] Password mismatch! Ignoring host %s!\n",inet_ntoa(agent->agentIP));
				}
			}
			free(encbuf); /* release TwoFishAlloc'ed buffer */
		}
		if(!(stationok && agent->persistentSocket))
			closesocket(agent->agentSocket);
	}while(again);
	return stationok;
}


/* removes spaces from a string 
*/	
void remspace(char *str)    
{	char *p;

	p=str;
	while(*p)
	{	if(myisspace(*p))		/* normalize spaces (tabs into space, etc) */
			*p=' ';
		p++;
	}
	while((p=strrchr(str,' ')))	/* remove spaces */
		strcpy(p,p+1);
}

/* parses duration arguments and returns seconds 
*/
unsigned long parseduration(char *p)  
{	unsigned long dur=0,tdu;
	char *tok,c1,c2;

	remspace(p);				/* remove spaces from value */
	while(*p)
	{	tok=p;
		while(*p && myisdigit(*p))
			p++;
		if(*p)
		{	c1=mytolower(*p);
			*p=0;
			p++;
			if(*p && !myisdigit(*p))
			{	c2=mytolower(*p++);
				while(*p && !myisdigit(*p))
					p++;
			}
			else
				c2=0;
			tdu=atol(tok);
			switch(c1)
			{	case 'm':	if(c2=='o')				/* for month... */
								tdu*=(60*60*24*30);	/* ...use 30 days */
							else
								tdu*=60;			/* minutes */
				case 's':	break;					/* seconds */
				case 'h':	tdu*=(60*60);			/* hours */
							break;
				case 'd':	tdu*=(60*60*24);		/* days */
							break;
				case 'w':	tdu*=(60*60*24*7);		/* week */
							break;
				case 'y':	tdu*=(60*60*24*365);	/* year */
							break;
			}
			dur+=tdu;
		}
		else
			dur+=atol(tok);
	}

	return dur;
}

/* This does nothing else than inet_ntoa, but it keeps 256 results in a static string
 * unlike inet_ntoa which keeps only one. This is used for (s)printf's were two IP
 * addresses are printed (this has been increased from four while multithreading the app).
*/
char *inettoa(unsigned long ip)
{	struct in_addr ips;
	static char addr[20];

	ips.s_addr=ip;
	strncpy(addr,inet_ntoa(ips),19);
	addr[19]=0;
	return addr;
}

int ExecuteResponseAction(char *arg)
{	char str[512],*p,*encbuf,*decbuf,*agentport,*agentpass,*agenthost;
	int i,error=TRUE,len,ipidx=0,peeridx=0;
	ActionRequestPacket actionRequest;
	struct hostent *hoste;
	unsigned long samip;
	ExecutorAgent agent;

			

	safecopy(str,arg);
	agenthost=str;
	agentport=NULL;
	agentpass=NULL;
	p=str;
	while(*p && *p!=':' && *p!='/') 
		p++;
	if(*p==':')
	{	*p++=0;
		if(*p)
			agentport=p;
		while(*p && *p!='/')
			p++;
	}
	if(*p=='/')
	{	*p++=0;
		if(*p)
			agentpass=p;
	}
	samip=0;
	if(inet_addr(agenthost)==INADDR_NONE)
	{	hoste=gethostbyname(agenthost);
		if(!hoste)
		{	fprintf(stderr,"Error: Unable to resolve host '%s', ignoring entry!\n",agenthost);
			return 1;
		}
		else
			samip=*(unsigned long *)hoste->h_addr;
	}
	else
	{	samip=inet_addr(agenthost);
		if(!samip)
		{	fprintf(stderr,"Error: Invalid host address '%s', ignoring entry!\n",agenthost);
			return 1;
		}
	}
	agent.agentIP.s_addr=samip;
	if(agentport!=NULL && atoi(agentport)>0)
		agent.agentPort=atoi(agentport);
	else
		agent.agentPort=FWSAM_DEFAULTPORT;
	if(agentpass!=NULL)
	{	strncpy(agent.agentKey ,agentpass,TwoFish_KEY_LENGTH);
		agent.agentKey [TwoFish_KEY_LENGTH]=0;
	}
	else
		agent.agentKey [0]=0;

	safecopy(agent.initialKey,agent.agentKey );
	agent.agentFish=TwoFishInit(agent.agentKey );

	agent.localSocketAddr.sin_port=htons(0);
	agent.localSocketAddr.sin_addr.s_addr=0;
	agent.localSocketAddr.sin_family=AF_INET;
	agent.stationSocketAddr.sin_port=htons(agent.agentPort);
	agent.stationSocketAddr.sin_addr=agent.agentIP;
	agent.stationSocketAddr.sin_family=AF_INET;

	do
		agent.mySeqNo=rand();
	while(agent.mySeqNo<20 || agent.mySeqNo>65500);
	agent.myKeyMod[0]=rand();
	agent.myKeyMod[1]=rand();
	agent.myKeyMod[2]=rand();
	agent.myKeyMod[3]=rand();
	agent.agentSeqNo=0;
	agent.persistentSocket=TRUE;
	agent.packetVersion=FWSAM_PACKETVERSION_PERSISTENT_CONN;
	
	if(CheckIn(&agent))
	{	error=FALSE;
	
		do
		{	ipidx=0;
			do
			{	if(!agent.persistentSocket)
				{	/* create a socket for the agent */
					agent.agentSocket=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP); 
					if(agent.agentSocket==INVALID_SOCKET)
					{	fprintf(stderr,"Error: [ExecuteResponseAction] Invalid Socket error!\n");
						error=TRUE;
					}
					if(bind(agent.agentSocket,(struct sockaddr *)&(agent.localSocketAddr),sizeof(struct sockaddr)))
					{	fprintf(stderr,"Error: [ExecuteResponseAction] Can not bind socket!\n");
						error=TRUE;
					}
				}
				else
					error=FALSE;
				if(!error)
				{	if(!agent.persistentSocket)
					{	/* let's connect to the agent*/
						if(connect(agent.agentSocket,(struct sockaddr *)&agent.stationSocketAddr,sizeof(struct sockaddr)))
						{	fprintf(stderr,"Error: [ExecuteResponseAction] Could not send responseaction to host %s.\n",inet_ntoa(agent.agentIP));
							closesocket(agent.agentSocket);
							error=TRUE;
						}
					}
					
					if(!error)
					{	if(verbose>0)
							printf("Info: [ExecuteResponseAction] Connected to host %s. %s IP %s.\n",inet_ntoa(agent.agentIP),blockMode==FWSAM_STATUS_BLOCK?"Executing action":"Undoing action",inettoa(blockIP[ipidx]));

						/* now build the packet */
						agent.mySeqNo+=agent.agentSeqNo; /* increase my seqno by adding agentseq no */
						actionRequest.endianCheck=1;						/* This is an endian indicator for Snortsam */
						actionRequest.airsSeqNo[0]=(char)agent.mySeqNo;
						actionRequest.airsSeqNo[1]=(char)(agent.mySeqNo>>8);
						actionRequest.agentSeqNo[0]=(char)agent.agentSeqNo;/* fill agent seqno */
						actionRequest.agentSeqNo[1]=(char)(agent.agentSeqNo>>8);	
						actionRequest.status=blockMode;			/* set type of message RESPONSE ACTION*/
						actionRequest.version=agent.packetVersion;			/* set packet version */
						actionRequest.duration[0]=(char)blockDuration;		/* set duration */
						actionRequest.duration[1]=(char)(blockDuration>>8);
						actionRequest.duration[2]=(char)(blockDuration>>16);
						actionRequest.duration[3]=(char)(blockDuration>>24);
						actionRequest.responseMode=blockFwType|blockHow|FWSAM_WHO_SRC; /* set the mode */
						actionRequest.dstIP[0]=(char)blockPeer[peeridx]; /* destination IP */
						actionRequest.dstIP[1]=(char)(blockPeer[peeridx]>>8);
						actionRequest.dstIP[2]=(char)(blockPeer[peeridx]>>16);
						actionRequest.dstIP[3]=(char)(blockPeer[peeridx]>>24);
						actionRequest.srcIP[0]=(char)blockIP[ipidx];	/* source IP */
						actionRequest.srcIP[1]=(char)(blockIP[ipidx]>>8);
						actionRequest.srcIP[2]=(char)(blockIP[ipidx]>>16);
						actionRequest.srcIP[3]=(char)(blockIP[ipidx]>>24);
						actionRequest.protocol[0]=(char)blockProto;	/* protocol */
						actionRequest.protocol[1]=(char)(blockProto>>8);/* protocol */

						actionRequest.plugin[0]=(char)blockPlugin;   /*plugin*/
						actionRequest.plugin[1]=(char)blockPlugin>>8;
						//Se empaqueta el nombre de usuario, si existe
						int i=0;
						char *puser = (char)0;
						puser = blockUser;
						while(puser && i<USERLENGTH){
							actionRequest.user[i++]=*puser++;
						}
						printf("Usuario empaquetado: %s\n", actionRequest.user);
						//Se empaqueta el campo adicional, si existe
						i=0;
						char *pad = (char)0;
						pad = blockAdditional;
						while(pad && i<ADLENGTH){
							actionRequest.adParam[i++]=*pad++;
						}
						printf("Adicional empaquetado: %s\n", actionRequest.adParam);

						if(blockProto==6 || blockProto==17)
						{	actionRequest.dstPort[0]=(char)blockPort;
							actionRequest.dstPort[1]=(char)(blockPort>>8);
						} 
						else
							actionRequest.dstPort[0]=actionRequest.dstPort[1]=0;
						actionRequest.srcPort[0]=actionRequest.srcPort[1]=0;

						actionRequest.sig_id[0]=(char)blockSID;		/* set signature ID */
						actionRequest.sig_id[1]=(char)(blockSID>>8);
						actionRequest.sig_id[2]=(char)(blockSID>>16);
						actionRequest.sig_id[3]=(char)(blockSID>>24);

						//#ifdef FWSAMDEBUG
							printf("Debug: [ExecuteResponseAction] Sending %s\n",blockMode==FWSAM_STATUS_BLOCK?"EXECUTE-ACTION":"UNDO-ACTION");
							printf("Debug: [ExecuteResponseAction] AIRSExecutor SeqNo:  %x\n",agent.mySeqNo);
							printf("Debug: [ExecuteResponseAction] ExecutorAgent SeqNo :  %x\n",agent.agentSeqNo);
							printf("Debug: [ExecuteResponseAction] Status     :  %i\n",blockMode);
							printf("Debug: [ExecuteResponseAction] Version    :  %i\n",agent.packetVersion);
							printf("Debug: [ExecuteResponseAction] Mode       :  %i\n",blockFwType|blockHow|FWSAM_WHO_SRC);
							printf("Debug: [ExecuteResponseAction] Duration   :  %li\n",blockDuration);
							printf("Debug: [ExecuteResponseAction] Protocol   :  %i\n",blockProto);
							printf("Debug: [ExecuteResponseAction] Src IP     :  %s\n",inettoa(blockIP[ipidx]));
							printf("Debug: [ExecuteResponseAction] Src Port   :  %i\n",0);
							printf("Debug: [ExecuteResponseAction] Dest IP    :  %s\n",inettoa(blockPeer[peeridx]));
							printf("Debug: [ExecuteResponseAction] Dest Port  :  %i\n",blockPort);
							printf("Debug: [ExecuteResponseAction] Sig_ID     :  %lu\n",blockSID);
							printf("Debug: [ExecuteResponseAction] Plugin     :  %i\n",blockPlugin);
							printf("Debug: [ExecuteResponseAction] User       :  %s\n",blockUser);
							printf("Debug: [ExecuteResponseAction] Ad. Param  :  %s\n",blockAdditional);
						//#endif

						encbuf=TwoFishAlloc(sizeof(ActionRequestPacket),FALSE,FALSE,agent.agentFish); /* get the encryption buffer */
						len=TwoFishEncrypt((char *)&actionRequest,(char **)&encbuf,sizeof(ActionRequestPacket),FALSE,agent.agentFish); /* encrypt the packet with current key */

						if(send(agent.agentSocket,encbuf,len,0)!=len) /* weird...could not send */
						{	fprintf(stderr,"Error: [ExecuteResponseAction] Could not send to host %s.\n",inet_ntoa(agent.agentIP));
							closesocket(agent.agentSocket);
							error=TRUE;
						}
						else
						{	i=FWSAM_NETWAIT;
							ioctlsocket(agent.agentSocket,FIONBIO,&i);	/* set non executing action and wait for  */
							while(i-- >1)							/* the response packet	 */
							{	waitms(10); /* wait for response (default maximum 3 secs */
								if(recv(agent.agentSocket,encbuf,len,0)==len)
									i=0; /* if we received packet we set the counter to 0. */
										 /* by the time we check with if, it's already dec'ed to -1 */
							}
							if(!i) /* id we timed out (i was one, then dec'ed)... */
							{	fprintf(stderr,"Error: [ExecuteResponseAction] Did not receive response from host %s.\n",inet_ntoa(agent.agentIP));
								closesocket(agent.agentSocket);
								error=TRUE;
							}
							else /* got a packet */
							{	decbuf=(char *)&actionRequest; /* get the pointer to the packet struct */
								len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent.agentFish); /* try to decrypt the packet with current key */

								if(len!=sizeof(ActionRequestPacket)) /* invalid decryption */
								{	safecopy(agent.agentKey ,agent.initialKey); /* try the intial key */
									TwoFishDestroy(agent.agentFish);
									agent.agentFish=TwoFishInit(agent.agentKey ); /* re-initialize the TwoFish with the intial key */
									len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent.agentFish); /* try again to decrypt */
									if(verbose>1)
										printf("Debug: [CheckOut] Had to use initial key!\n");
								}
								if(len==sizeof(ActionRequestPacket)) /* valid decryption */
								{	if(actionRequest.version==agent.packetVersion)/* master speaks my language */
									{	if(actionRequest.status==FWSAM_STATUS_OK || actionRequest.status==FWSAM_STATUS_NEWKEY 
										|| actionRequest.status==FWSAM_STATUS_RESYNC || actionRequest.status==FWSAM_STATUS_HOLD) 
										{	agent.agentSeqNo=actionRequest.agentSeqNo[0] | (actionRequest.agentSeqNo[1]<<8); /* get stations seqno */
											agent.lastContact=(unsigned long)time(NULL); /* set the last contact time (not used yet) */
											if(verbose>1)
											{
												printf("Debug: [ExecuteResponseAction] Received %s\n",actionRequest.status==FWSAM_STATUS_OK?"OK":
																						  actionRequest.status==FWSAM_STATUS_NEWKEY?"NEWKEY":
																					      actionRequest.status==FWSAM_STATUS_RESYNC?"RESYNC":
																					      actionRequest.status==FWSAM_STATUS_HOLD?"HOLD":"ERROR");
												printf("Debug: [ExecuteResponseAction] AIRSExecutor SeqNo:  %x\n",actionRequest.airsSeqNo[0]|(actionRequest.airsSeqNo[1]<<8));
												printf("Debug: [ExecuteResponseAction] ExecutorAgent SeqNo :  %x\n",agent.agentSeqNo);
												printf("Debug: [ExecuteResponseAction] Status     :  %i\n",actionRequest.status);
												printf("Debug: [ExecuteResponseAction] Version    :  %i\n",actionRequest.version);
											}

											if(actionRequest.status==FWSAM_STATUS_HOLD)
											{	i=FWSAM_NETHOLD;			/* Stay on hold for a maximum of 60 secs (default) */
												while(i-- >1)							/* the response packet	 */
												{	waitms(10); /* wait for response  */
													if(recv(agent.agentSocket,encbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,0)==sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE)
													  i=0; /* if we received packet we set the counter to 0. */
										 		}
												if(!i) /* id we timed out (i was one, then dec'ed)... */
												{	fprintf(stderr,"Error: [ExecuteResponseAction] Did not receive response from host %s.\n",inet_ntoa(agent.agentIP));
													error=TRUE;
													actionRequest.status=FWSAM_STATUS_ERROR;
												}
												else /* got a packet */
												{	decbuf=(char *)&actionRequest; /* get the pointer to the packet struct */
													len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent.agentFish); /* try to decrypt the packet with current key */

													if(len!=sizeof(ActionRequestPacket)) /* invalid decryption */
													{	safecopy(agent.agentKey ,agent.initialKey); /* try the intial key */
														TwoFishDestroy(agent.agentFish);
														agent.agentFish=TwoFishInit(agent.agentKey ); /* re-initialize the TwoFish with the intial key */
														len=TwoFishDecrypt(encbuf,(char **)&decbuf,sizeof(ActionRequestPacket)+TwoFish_BLOCK_SIZE,FALSE,agent.agentFish); /* try again to decrypt */
														if(verbose>0)
															printf("Info: [ExecuteResponseAction] Had to use initial key again!\n");
													}
													if(verbose>1)
													{	printf("Debug: [ExecuteResponseAction] Received %s\n", actionRequest.status==FWSAM_STATUS_OK?"OK":
																								   actionRequest.status==FWSAM_STATUS_NEWKEY?"NEWKEY":
																								   actionRequest.status==FWSAM_STATUS_RESYNC?"RESYNC":
																								   actionRequest.status==FWSAM_STATUS_HOLD?"HOLD":"ERROR");
														printf("Debug: [ExecuteResponseAction] AIRSExecutor SeqNo:  %x\n",actionRequest.airsSeqNo[0]|(actionRequest.airsSeqNo[1]<<8));
														printf("Debug: [ExecuteResponseAction] ExecutorAgent SeqNo :  %x\n",agent.agentSeqNo);
														printf("Debug: [ExecuteResponseAction] Status     :  %i\n",actionRequest.status);
														printf("Debug: [ExecuteResponseAction] Version    :  %i\n",actionRequest.version);
													}
													if(len!=sizeof(ActionRequestPacket)) /* invalid decryption */
													{	fprintf(stderr,"Error: [ExecuteResponseAction] Password mismatch! Ignoring host %s.\n",inet_ntoa(agent.agentIP));
														error=TRUE;
														actionRequest.status=FWSAM_STATUS_ERROR;
													}
													else if(actionRequest.version!=agent.packetVersion) /* invalid protocol version */
													{	fprintf(stderr,"Error: [ExecuteResponseAction] Protocol version error! Ignoring host %s.\n",inet_ntoa(agent.agentIP));
														error=TRUE;
														actionRequest.status=FWSAM_STATUS_ERROR;
													}
													else if(actionRequest.status!=FWSAM_STATUS_OK && actionRequest.status!=FWSAM_STATUS_NEWKEY && actionRequest.status!=FWSAM_STATUS_RESYNC) 
													{	fprintf(stderr,"Error: [ExecuteResponseAction] Funky handshake error! Ignoring host %s.\n",inet_ntoa(agent.agentIP));
														error=TRUE;
														actionRequest.status=FWSAM_STATUS_ERROR;
													}
												}
											}
											if(actionRequest.status==FWSAM_STATUS_RESYNC)  /* if agent want's to resync... */
											{	safecopy(agent.agentKey ,agent.initialKey); /* ...we use the intial key... */
												memcpy(agent.agentKeyMod,actionRequest.duration,4);	 /* and note the random key modifier */
											}
											if(actionRequest.status==FWSAM_STATUS_NEWKEY || actionRequest.status==FWSAM_STATUS_RESYNC)	
											{	
												GenerateNewCipherKey(&agent,&actionRequest); /* generate new TwoFish keys */
												if(verbose>1)
													printf("Debug: [ExecuteResponseAction] Generated new encryption key...\n");
											}
											if(!agent.persistentSocket)
												closesocket(agent.agentSocket);
										}
										else if(actionRequest.status==FWSAM_STATUS_ERROR) /* if ExecutorAgent reports an error on second try, */
										{	closesocket(agent.agentSocket);				  /* something is messed up and ... */
											error=TRUE;
											fprintf(stderr,"Error: [ExecuteResponseAction] Undetermined error right after CheckIn! Ignoring host %s.",inet_ntoa(agent.agentIP));
										}
										else /* an unknown status means trouble... */
										{	fprintf(stderr,"Error: [ExecuteResponseAction] Funky handshake error! Ignoring host %s.",inet_ntoa(agent.agentIP));
											closesocket(agent.agentSocket);
											error=TRUE;
										}
									}
									else   /* if the ExecutorAgent agentuses a different packet version, we have no choice but to ignore it. */
									{	fprintf(stderr,"Error: [ExecuteResponseAction] Protocol version error! Ignoring host %s.",inet_ntoa(agent.agentIP));
										closesocket(agent.agentSocket);
										error=TRUE;
									}
								}
								else /* if the intial key failed to decrypt as well, the keys are not configured the same, and we ignore that ExecutorAgent agent. */
								{	fprintf(stderr,"Error: [ExecuteResponseAction] Password mismatch! Ignoring host %s.",inet_ntoa(agent.agentIP));
									closesocket(agent.agentSocket);
									error=TRUE;
								}
							}
						}
						free(encbuf); /* release of the TwoFishAlloc'ed encryption buffer */
					}
				}
				
				ipidx++;
			}while(!error && ipidx<NUM_HOSTS && blockIP[ipidx]);
			peeridx++;
		}while(!error && peeridx<NUM_HOSTS && blockPeer[peeridx]);

		if(!error)
		{	if(checkout)
				CheckOut(&agent);
			else
			{	closesocket(agent.agentSocket);
				agent.persistentSocket=FALSE;
			}
		}
	}
	TwoFishDestroy(agent.agentFish);

	return error;
}

void exittool(int err)
{
#ifdef WIN32
	WSACleanup();
#endif
	exit(err);
}

void header(void)
{	char str[52];
	static int printed=FALSE;
	
	if(verbose && !printed)
	{	safecopy(str,SAMTOOL_REV+11);
	    str[strlen(SAMTOOL_REV+11)-2]=0;
		printf("\nAIRS Response Action Executor --> Version: %s\n\nCopyright (c) 2005-2008 Frank Knobbe <frank@knobbe.us>. All rights reserved.\n\n 2012 Danny Guamán. Some modifications were made on the original source code. It was used as part of a research project at the Polytechnic University of Madrid\n\n",str);
		printed=TRUE;
	}
}
	//****************************************************************************MAIN***Debe llamar el MCER proveyendo los parametros necesarios
int main(int argc, char **argv)
{	int curarg,i,retval=0;
	char *p,str[52];
	struct hostent *hoste;
	struct protoent *protoe;
#ifdef WIN32
	struct WSAData wsad;
#endif
	
	curarg=1;

#ifdef WIN32
	if(WSAStartup(MAKEWORD(1,1),&wsad))				/* intialize winsock */
	{	printf("\nCould not initialize Winsock!\n");
		exit(1);
	}
	if(LOBYTE(wsad.wVersion)!=1 || HIBYTE(wsad.wVersion)!=1)
	{	printf("\nThis Winsock version is not supported!\n");
	    exit(1);
	}
#endif
	
	while(curarg<argc)
	{	p=argv[curarg];
		if(*p=='-')
		{	while(*p=='-')
				p++;
			if(!strncmp(p,"e",1))
			{	blockMode=FWSAM_STATUS_BLOCK;
				curarg++;
			}
			else if(!strncmp(p,"un",2))
			{	blockMode=FWSAM_STATUS_UNBLOCK;
				curarg++;
			}
			else if(!strcmp(p,"v"))
			{	verbose=1;
				curarg++;
			}
			else if(!strcmp(p,"vv"))
			{	verbose=2;
				curarg++;
			}
			else if(!strcmp(p,"n"))
			{	checkout=FALSE;
				curarg++;
			}
			else if(!strcmp(p,"h"))
			{	verbose=TRUE;
				//header();
				printf("\n");
				exittool(0);
			}
			else if(!strncmp(p,"i",1) || !strncmp(p,"at",2))
			{	if(++curarg <argc)
				{	if(inet_addr(argv[curarg])==INADDR_NONE)
					{	hoste=gethostbyname(argv[curarg]);
						if (!hoste) 
						{	fprintf(stderr,"Error: Unable to resolve responseaction host '%s'!\n",argv[curarg]);
							exittool(11);
						}
						else
						{	i=0;
							do
							{	if(hoste->h_addr_list[i])
									blockIP[i]=*((unsigned long *)hoste->h_addr_list[i]);
								else
									blockIP[i]=0;
								i++;
							}while(i<NUM_HOSTS && blockIP[i-1]);
						}
					} 
					else
					{	blockIP[0]=inet_addr(argv[curarg]);
						blockIP[1]=0;
						if(!blockIP[0])
						{	fprintf(stderr,"Error: Invalid response-action address '%s'!\n",argv[curarg]);
							exittool(12);
						}
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Response-action host not specified!\n");
					exittool(22);
				}
			}
			else if(!strncmp(p,"pe",2))
			{	if(++curarg <argc)
				{	if(inet_addr(argv[curarg])==INADDR_NONE)
					{	hoste=gethostbyname(argv[curarg]);
						if (!hoste) 
						{	fprintf(stderr,"Error: Unable to resolve peer host '%s'!\n",argv[curarg]);
							exittool(13);
						}
						else
						{	i=0;
							do
							{	if(hoste->h_addr_list[i])
									blockPeer[i]=*((unsigned long *)hoste->h_addr_list[i]);
								else
									blockPeer[i]=0;
								i++;
							}while(i<NUM_HOSTS && blockPeer[i-1]);
						}
					} 
					else
					{	blockPeer[0]=inet_addr(argv[curarg]);
						blockPeer[1]=0;
						if(!blockIP[0])
						{	fprintf(stderr,"Error: Invalid peer address '%s'!\n",argv[curarg]);
							exittool(14);
						}
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Peer IP not specified!\n");
					exittool(24);
				}
			}
			else if(!strncmp(p,"pr",2))
			{	if(++curarg <argc)
				{	if(atol(argv[curarg])>0)
						blockProto=atol(argv[curarg])&65535;
					else
					{	protoe=getprotobyname(argv[curarg]);
						if(!protoe)
						{	fprintf(stderr,"Error: Invalid protocol '%s'!\n",argv[curarg]);
							exittool(16);
						}
						blockProto=protoe->p_proto;
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Protocol not specified!\n");
					exittool(26);
				}
			}
			else if(!strncmp(p,"po",2) || !strncmp(p,"dp",2) || !strncmp(p,"dst",3) || !strncmp(p,"dest",4))
			{	if(++curarg <argc)
				{	if(atol(argv[curarg])>0)
						blockPort=atol(argv[curarg])&65535;
					else
					{	fprintf(stderr,"Error: Invalid port specified!\n");
						exittool(15);
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Port not specified!\n");
					exittool(26);
				}
			}
			else if(!strncmp(p,"pl",2))
			{	if(++curarg <argc)
				{	if(atol(argv[curarg])>0)
						blockPlugin=atol(argv[curarg])&65535;
					else
					{	fprintf(stderr,"Error: Invalid plugin specified!\n");
						exittool(15);
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Plugin not specified!\n");
					exittool(26);
				}
			}
			else if(!strncmp(p,"du",2))
			{	if(++curarg <argc)
				{	safecopy(str,argv[curarg]);
					blockDuration=parseduration(str);
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Duration not specified!\n");
					exittool(27);
				}
			}
			else if(!strncmp(p,"sid",3) || !strncmp(p,"id",2))
			{	if(++curarg <argc)
				{	blockSID=atol(argv[curarg]);
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: SID not specified!\n");
					exittool(28);
				}
			}
			else if(!strncmp(p,"mo",2))
			{	if(++curarg <argc)
				{	if(!strcmp(argv[curarg],"in"))
						blockHow=FWSAM_HOW_IN;
					else if(!strcmp(argv[curarg],"out"))
						blockHow=FWSAM_HOW_OUT;
					else if(!strcmp(argv[curarg],"inout") || !strcmp(argv[curarg],"both") || !strcmp(argv[curarg],"full"))
						blockHow=FWSAM_HOW_INOUT;
					else if(!strcmp(argv[curarg],"this") || !strncmp(argv[curarg],"conn",4))
						blockHow=FWSAM_HOW_THIS;
					else
					{	fprintf(stderr,"Error: Invalid direction specified!\n");
						exittool(17);
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Direction not specified!\n");
					exittool(29);
				}
		 	}
		 	else if(!strncmp(p,"user",4))
			{	if(++curarg <argc)
				{	
					blockUser = argv[curarg];
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: User name not specified!\n");
					exittool(29);
				}
		 	}
		 	else if(!strncmp(p,"aditional",2))
			{	if(++curarg <argc)
				{	
					blockAdditional = argv[curarg];
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Aditional param not specified!\n");
					exittool(29);
				}
		 	}
			else if(!strncmp(p,"w",1))
			{	if(++curarg <argc)
				{	if(!strcmp(argv[curarg],"src"))
						blockWho=FWSAM_WHO_SRC;
					else if(!strcmp(argv[curarg],"dst"))
						blockHow=FWSAM_WHO_DST;
					else
					{	fprintf(stderr,"Error: Invalid direction specified!\n");
						exittool(17);
					}
					curarg++;
				}
				else
				{	fprintf(stderr,"Error: Who param not specified!\n");
					exittool(29);
				}
			}
			else
			{	fprintf(stderr,"Error: Invalid option specified!\n");
				exittool(19);
			}
		}
		else
		{	if(!blockIP[0])
			{	fprintf(stderr,"Error: Response Action IP Address not specified!\n");
				exittool(40);
			}
			if(blockHow==FWSAM_HOW_THIS)
			{	if(!blockPeer[0])
				{	fprintf(stderr,"Error: Peer IP address not specified!\n");
					exittool(41);
				}
				if(!blockPort)
				{	fprintf(stderr,"Error: Destination port not specified!\n");
					exittool(42);
				}
				if(!blockProto)
				{	fprintf(stderr,"Error: IP protocol not specified!\n");
					exittool(43);
				}
			}
			//header();
			retval|=ExecuteResponseAction(argv[curarg]);
			curarg++;
		}
	}
#ifdef WIN32
        WSACleanup();
#endif
	return retval;
}

#undef FWSAMDEBUG
#endif /* __AIRSEXECUTOR_C__ */
