#include <time.h>
#include <stdlib.h>
#include <stdio.h>


#include "link_mining.h"

char *nodeXML =
    "<feed><version>1.0</version><feedType>100020001</feedType>"
    "<srcJid>link_mining_datoutie.gozap.com</srcJid><createTime></createTime>"
    "<destRange><destJids></destJids></destRange>"
    "<data></data></feed>";
char *dataXML =
    "<item><srcJid></srcJid><connJid></connJid><type></type></item>";
/*
typedef struct redis_reply_array_st
{
    char **elements;
    int size;
}
    rep_arr_st;

static void array_st_free(void *array)
{
    rep_arr_st *ar = (rep_arr_st *)array;
    int i;
    if(ar){
	if(ar->size > 0){
	    for(i = 0 ; i < ar->size ;i++){
		if(ar->elements[i])
		    free(ar->elements[i]);
	    }
	    free(ar->elements);
	}
	free(ar);
    }
}
*/
static unsigned long long lm_ustime()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec*1000000+tv.tv_usec;
}
static void lm_add_timestamp(xode parent)
{
    char cdata[64];
    snprintf(cdata,sizeof(cdata),"%ld",lm_ustime());
    xode createTime = xode_get_tag(parent,"createTime");
    if(createTime == NULL)
	createTime = xode_insert_tag(parent,"createTime");
    xode_insert_cdata(createTime,cdata,strlen(cdata));
    return;
}


static void send_xml(global_conf_st* g_conf,char *str)
{
    memcached_return_t rc;
    jlog(L_ERR,"get memcached conns");
    memcached_st *mq = memcached_pool_pop(g_conf->send_mq->mq_pool,1,&rc);
    char *key = g_conf->send_mq->task_name;
    char *val = str;
    int i = 0;
    do{
	rc = memcached_set(mq,key,strlen(key),val,strlen(val),
			   (time_t)0,(uint32_t)0);
	if(MEMCACHED_SUCCESS != rc){
	    jlog(L_ERR,"set memcache error rc is %d retry %d key->%s,val->%s",
		 rc,i++,key,val);
	}
    }while (rc == MEMCACHED_ERRNO || rc == MEMCACHED_UNKNOWN_READ_FAILURE);
    memcached_pool_push(g_conf->send_mq->mq_pool,mq);
    jlog(L_ERR,"push to mq_pool");
    jlog(L_INFO,"send msg %s \n",str);
}

static char *following_key(char *key)
{
    return generate_key("{datoutie:relation:%s}:following",key);
}

static char *conn_key(char *key)
{
    return generate_key("{datoutie:relation:%s}:connection",key);
}


static void send_msg(global_conf_st* g_conf,char *key_pnum,char *conn_pnum,char * link_type)
{
    xode header = xode_from_str(nodeXML,strlen(nodeXML));
    xode data = xode_from_str(dataXML,strlen(dataXML));
    if(NULL == header || NULL == data ){
	jlog(L_ERR,"create xode error\n");
	return;
    }
    char key_jid[64],conn_jid[64];
    snprintf(key_jid,sizeof(key_jid),"%s@datoutie.com",key_pnum);
    snprintf(conn_jid,sizeof(conn_jid),"%s@datoutie.com",conn_pnum);
    //xode feed = xode_get_tag(header,"feed");
    lm_add_timestamp(header);
    xode destRange = xode_get_tag(header,"destRange");
    xode destJids = xode_get_tag(destRange,"destJids");
    xode_insert_cdata(destJids,key_jid,strlen(key_jid));
    
    //xode item = xode_get_tag(data,"item");
    xode srcJid = xode_get_tag(data,"srcJid");
    xode_insert_cdata(srcJid,key_jid,strlen(key_jid));
    xode connJid = xode_get_tag(data,"connJid");
    xode_insert_cdata(connJid,conn_jid,strlen(conn_jid));
    xode type = xode_get_tag(data,"type");
    xode_insert_cdata(type,link_type,strlen(link_type));
    xode feed_data = xode_get_tag(header,"data");
    xode_insert_node(feed_data,data);
    char *msg_str = xode_to_str(header);
    send_xml(g_conf,msg_str);
    fprintf(stderr,"send feed to send_mq: %s\n",msg_str);
    xode_free(header);
    return ;
}

static void send_conn_msg(global_conf_st* g_conf,char *key_pnum,char *conn_pnum)
{
    send_msg(g_conf,key_pnum,conn_pnum,"0");
}


static void send_following_msg(global_conf_st* g_conf,char *src_pnum,char *dst_pnum,int type)
{
    char typestr[10];
    snprintf(typestr,sizeof(typestr),"%d",type);
    send_msg(g_conf,src_pnum,dst_pnum,typestr);
    return ;
}


static int check_connection(global_conf_st* g_conf,char *key_pnum,char *conn_pnum)
{
    char *key = conn_key(key_pnum);
    char *val = conn_pnum;
    int i ;
    redis_return_st rc;
    i = redis_sismember(g_conf->redis_client,key,val,&rc);
    free(key);
    return i;
}

static int check_following(global_conf_st* g_conf,char *key_pnum,char *foll_pnum)
{
    char *key = following_key(key_pnum);
    char *val = foll_pnum;
    int i ;
    redis_return_st rc;
    i = redis_sismember(g_conf->redis_client,key,val,&rc);
    free(key);
    return i;
}


static int add_conn(global_conf_st* g_conf,char *key_pnum,char *conn_pnum)
{
    //add
    char *key = conn_key(key_pnum);
    char *val = conn_pnum;
    redis_return_st rc;
    rc = redis_sadd(g_conf->redis_client,key,val);
    if(rc != REDIS_SUCCESS){
	jlog(L_ERR,"add_conn error %s %s\n",key_pnum,conn_pnum);
	return -1;
    }
    //send
    send_conn_msg(g_conf,key_pnum,conn_pnum);
    return 0;
}

static int add_following(global_conf_st* g_conf,char *key_pnum,char *foll_pnum)
{
    //add
    char *key = following_key(key_pnum);
    char *val = foll_pnum;
    redis_return_st rc;
    rc = redis_sadd(g_conf->redis_client,key,val);
    if(rc != REDIS_SUCCESS){
	jlog(L_ERR,"foll_conn error %s %s\n",key_pnum,foll_pnum);
	return -1;
    }
    //send
    send_following_msg(g_conf,key_pnum,foll_pnum,1);
    send_following_msg(g_conf,foll_pnum,key_pnum,2);
    return 0;
}


void do_link_mining(global_conf_st* g_conf,msg_st *msg)
{
    if(g_conf == NULL || msg == NULL)
	return;
    if(check_connection(g_conf,msg->dst_pnum,msg->src_pnum)){
	add_conn(g_conf,msg->src_pnum,msg->dst_pnum);
	//send_conn_msg(g_conf,msg->src_pnum,msg->dst_pnum);
    }
    else if(check_following(g_conf,msg->dst_pnum,msg->src_pnum)){
	add_conn(g_conf,msg->src_pnum,msg->dst_pnum);
	//send_conn_msg(g_conf,msg->src_pnum,msg->dst_pnum);
	add_conn(g_conf,msg->dst_pnum,msg->src_pnum);
	//send_conn_msg(g_conf,msg->dst_pnum,msg->src_pnum);
    }
    else{
	add_following(g_conf,msg->src_pnum,msg->dst_pnum);
	//send_following_msg(g_conf,msg->src_pnum,msg->dst_pnum,1);
	//send_following_msg(g_conf,msg->dst_pnum,msg->src_pnum,2);
    }
}


