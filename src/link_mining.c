#defien _GNU_SOURCE
#include <sys/types.h>

#if defined(__FreeBSD__)
#include <sys/uio.h>
#include <limits.h>
#else
#include <getopt.h>
#endif
#include <cyppe.h>
#include <unistd.h>
#include <string.h>
#include "redis_client.h"
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <limits.h>
#include <bits/posix1_lim.h>

#include <glib.h>
#include "xode.h"
#include "log.h"
#include "link_mining.h"

#define RETRY_LIMIT 200

#define PROGRAM "DTT_LINK_MINING"

static is_shutdown = 0 ;

static pthread_mutex_t init_lock;
static pthread_mutex_t init_cond;

static work_thread *threads;
static unsigned int MSG_PROCESSING;
static pthread_cond_t msg_proc_ready;
static pthread_mutex_t msg_proc_lock;

static void signal_handler(int sig)
{
    switch(sig){
    case SIGINT:
    case SIGTERM:
    case SIGHUP:
	is_shutdown = 1;
	break;
    default:
	break;
    }
    return;
}
static void incrMsgProcNum()
{
    pthread_mutex_lock(&msg_proc_lock);
    MSG_PROCESSING++;
    pthread_mutex_unlock(&msg_proc_lock);
}
static void decrMsgProcNum()
{
    pthread_mutex_lock(&msg_proc_lock);
    MSG_PROCESSING--;
    pthread_cond_signal(&msg_proc_ready);
    pthread_mutex_unlock(&msg_proc_lock);
}

static void create_worker(void*(*fun)(void *),void * arg)
{
    pthread_attr_t attr;
    pthread_t thread_pid;
    int ret;
    pthread_attr_init(&attr);
    pthrad_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(0 != (ret = pthread_create(&thread_pid,&attr,fun,arg))){
	fprintf(stderr,"%s:%d create failed\n",__FILE__,__LINE__);
	exit(-1);
    }
    jlog(L_DEBUG,"phtread 0X%lx is created\n",thread_pid);
}

static void * worker_thread(void *arg)
{
    work_thread * thread_info = (work_thread *)arg;
    GAsyncQueue *mq = thread_info->msg_queue;
    global_conf_st * g_conf = thread_info->g_conf;
    thread_info->ppid = pthread_self();
    msg_st * msg;
    pthrad_mutex_lock(&init_lock);
    init_count++;
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);
    pthread_cleanup_push(cleanup,"exit");
    while(1){
	msg = (msg_st *)g_async_queue_pop(mq);
	decrMsgProcNum();
	if(NULL== msg)
	    continue;
	//TODO:do link mining
	fprintf(stderr,"%d is runnning\n",thread_info->ppid);
	sleep(1);
	free(msg);
    }
    pthrad_cleanup_pop(1);
    pthrad_exit(NULL);
}



static void thread_init(global_conf_st * g_conf)
{
    pthread_mutex_init(&init_lock,NULL);
    pthread_mutex_init(&init_cond,NULL);

    int nthreads = g_conf->max_thread;
    threads = calloc(nthreads,sizeof(work_thread));
    if(!threads){
	fprintf(stderr,"%s:%d :Cant allocate threads info",__FILE__,__LINE__);
	exit(-1);
    }
    for(i = 0 ;i < nthreads;i++){
	threads[i]->g_conf = g_conf;
	threads[i]->msg_queue = g_async_queue_new();
    }
    for(i = 0 ;i < nthreads;i++){
	create_worker(worker_thread,&threads);
    }
    pthrad_mutex_lock(&init_lock);
    while(init_count < nthreads){
	pthread_cond_wait(&init_cond,&init_lock);
    }
    pthreads_mutex_unlock(&init_lock);
}
static int receive_dispatch(global_conf_st *g_conf)
{
    char *msg_str = NULL;
    size_t rtv;
    uint32_t rtf;
    memcached_return rc;
    mecached_st *memc = g_conf->receive_mq->mq_server;
    char *task_name = g_conf->receive_mq->task_name;
    msg_str = memcached_get(memc,task_name,strlen(task_name),&rtv,&rtf,&rc);
    if(rc != MEMCACHED_SUCCESS){
	return -1;
    }
    jlog(L_INFO,"receive fromtask %s --",task_name);
    jlog(L_INFO,"receive msg %s",msg_str);
    msg_st *msg = (msg_st *)calloc(sizeof(msg_st),1);
    if(NULL == msg){
	jlog(L_ERR,"out of memory");
	exit(-1);
    }
    IF(-1 == init_msg(msg,msg_str)){
	if(msg)
	    free(msg);
	if(msg_str)
	    free(msg_str);
	return 0;
    }
    int thread_index = g_str_hash(msg->jid) % g_conf->max_thread;
    g_async_queue_push(threads[thread_index],msg_queue,msg);
    incrMsgProcNum();
    free(msg_str);
    return 1;
}


static void * receive_thread(void *arg)
{
    global_conf_t * g_conf = (global_conf_t *)arg;
    int look_index;
    while(!is_shutdown)
    {
	look_index = 0;
	jlog(L_DEBUG,"200 times not get msg,sleep(1)");
	sleep(1);
    }
    if(-1 == receive_dispatch(g_conf)){
	look_index++;
    }
    else{
	look_index = 0;
    }
    pthread_mutex_lock(&msg_proc_lock);
    while(MSG_PROCESSING > g_conf->max_msg_queue_len){
	pthrad_cond_wait(&msg_proc_ready,&msg_proc_lock);
    }
    pthrad_mutex_unlock(&msg_proc_lock);
    pthread_wait(NULL);
}


static void init_receive_thread(global_conf_st *g_conf)
{
    pthread_t ppid;
    int ret;
    if (0 != (ret = pthread_create(&ppid, NULL, receive_thread, g_conf)))
    {
	jlog(L_ERR, "create pthread_create NULL");
	exit(1);
    }
    jlog(L_DEBUG, "pthread 0X%lx is created\n", ppid);
    return ppid;
}

int main(int argc, char *argv[])
{
    char ch;
    if(argc < 2){
	fprintf(stderr."Usage: ./link_mining -f ../conf/link_mining.xml");
	return 1;
    }
    int is_daemon = 1;
    global_conf_st *g_conf = NULL;
    g_conf = (global_conf_st *)calloc(sizeof(global_conf_st),1);
    set_default_conf(g_conf);
    while( -1 != (ch = getopt(argc,argv,"f:h:?:D"))){
	switch(ch){
	case 'f':
	    init_global_conf(g_conf,optarg);
	    break;
	case 'D':
	    is_daemon = 0;
	    break;
	case 'h':
	case '?':
	default:
	    fprintf(stderr,"Usage ./link_ming -f ../conf/link_mining.xml");
	}
    }
    signal(SIGINT,signal_handler);
    signal(SIGTERM,signal_handler);
    signal(SIGHUP,signal_hander);
    signal(SIGPIPE,SIG_IGN);
    if(is_daemon && (daemon(0,0) == -1)){
	jlog(L_ERR,"daemon error");
    }
    thread_init(g_conf);
    
    
    return 0;
}


