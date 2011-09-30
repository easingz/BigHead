#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/types.h>
#include<libmemcached/memcached.h>
#include<sys/time.h>
#include<unistd.h>

long timer(int reset)
{
    static long start = 0;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    /* return timediff */
    if (!reset)
    {
	long stop = ((long) tv.tv_sec) * 1000 + tv.tv_usec / 1000;
	return (stop - start);
    }

    /* reset timer */
    start = ((long) tv.tv_sec) * 1000 + tv.tv_usec / 1000;

    return 0;
}

int main(int argc, char *argv[])
{
    memcached_st *send_mq,*receive_mq;
    memcached_return rc;
    memcached_server_st *servers;
    send_mq = memcached_create(NULL);
    receive_mq = memcached_create(NULL);
    servers = memcached_server_list_append(NULL,"localhost",21201,&rc);
    rc = memcached_server_push(receive_mq,servers);
    if(rc != MEMCACHED_SUCCESS){
	fprintf(stdout,"receive queue init failed,exit!\n");
	exit(1);
    }
    fprintf(stdout,"receive queue init successfully!\n");
    servers = memcached_server_list_append(NULL,"localhost",21202,&rc);
    rc = memcached_server_push(send_mq,servers);
    if(rc != MEMCACHED_SUCCESS){
	fprintf(stdout,"send queue init failed,exit!\n");
	exit(1);
    }
    fprintf(stdout,"send queue init successfully!\n");
    memcached_server_free(servers);
    
    char *key = "datoutie_test_queue";
    char *val_1 = "12345678912,21987654321";// A->B
    char *val_2 = "12345678912,78945612378";// A->C
    char *val_3 = "21987654321,12345678912";// B->A
    char *val_4 = "32165498798,12345678912";// D->A
    rc = memcached_set(receive_mq,key,strlen(key),val_1,strlen(val_1),(time_t)0,(uint32_t)0);
    rc = memcached_set(receive_mq,key,strlen(key),val_2,strlen(val_2),(time_t)0,(uint32_t)0);
    rc = memcached_set(receive_mq,key,strlen(key),val_3,strlen(val_3),(time_t)0,(uint32_t)0);
    rc = memcached_set(receive_mq,key,strlen(key),val_4,strlen(val_4),(time_t)0,(uint32_t)0);
    char *result[8];
    result[0] = "<destRange><destJids>12345678912@datoutie.com</destJids></destRange><data><item><srcJid>12345678912@datoutie.com</srcJid><connJid>21987654321@datoutie.com</connJid><type>1</type></item></data>";//feed to A that A->B
    result[1] = "<destRange><destJids>21987654321@datoutie.com</destJids></destRange><data><item><srcJid>21987654321@datoutie.com</srcJid><connJid>12345678912@datoutie.com</connJid><type>2</type></item></data>";//feed to B that B<-A
    result[2] = "<destRange><destJids>12345678912@datoutie.com</destJids></destRange><data><item><srcJid>12345678912@datoutie.com</srcJid><connJid>78945612378@datoutie.com</connJid><type>1</type></item></data>";//feed to A that A->C
    result[3] = "<destRange><destJids>78945612378@datoutie.com</destJids></destRange><data><item><srcJid>78945612378@datoutie.com</srcJid><connJid>12345678912@datoutie.com</connJid><type>2</type></item></data>";//feed to C that C<-A
    result[4] = "<destRange><destJids>21987654321@datoutie.com</destJids></destRange><data><item><srcJid>21987654321@datoutie.com</srcJid><connJid>12345678912@datoutie.com</connJid><type>0</type></item></data>";//feed to B that B<->A
    result[5] = "<destRange><destJids>12345678912@datoutie.com</destJids></destRange><data><item><srcJid>12345678912@datoutie.com</srcJid><connJid>21987654321@datoutie.com</connJid><type>0</type></item></data>";//feed to A that A<->B
    result[6] = "<destRange><destJids>32165498798@datoutie.com</destJids></destRange><data><item><srcJid>32165498798@datoutie.com</srcJid><connJid>12345678912@datoutie.com</connJid><type>1</type></item></data>";//feed to D that D->A
    result[7] = "<destRange><destJids>12345678912@datoutie.com</destJids></destRange><data><item><srcJid>12345678912@datoutie.com</srcJid><connJid>32165498798@datoutie.com</connJid><type>2</type></item></data>";//feed to A that A<-D
    int i ,j;
    size_t rtv;
    uint32_t rtf;
    char *found = NULL;
    char *msg_str[8];
    sleep(10);
    for(i = 0;i < 8;i++){
	msg_str[i] = memcached_get(send_mq,key,strlen(key),&rtv,&rtf,&rc);
    }
    for(i = 0;i < 8;i++){
	for(j = 0;j < 8;j++){
	    
	    if(NULL != (found = strstr(result[i],msg_str[j]))){
		fprintf(stderr,"passed 1 test ,totally 8 cases.\n");
		break;
	    }
	}
	if (NULL == found){
	    fprintf(stderr,"failed 1 test ,totally 8 cases.\n");
	    fprintf(stderr,"expect result:  %s \n",result[i]);
	    fprintf(stderr,"got result:     %s \n",msg_str[i]);
	}
	found = NULL;
    }
    for(i = 0;i < 8;i++){
	free(msg_str[i]);
    }
    return 0;
}
