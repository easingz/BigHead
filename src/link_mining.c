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



static void memcacheq_st_free(memcacheq_st *mq)
{
    if (mq)
    {
	if (mq->mq_server)
	{
	    memcached_free(mq->mq_server);
	}
	if (mq->mq_pool)
	{
	    memcached_pool_destroy(mq->mq_pool);
	}
	free(mq);
    }
}

static void global_conf_free(global_conf_st *g_conf)
{
    if (g_conf)
    {
	memcacheq_st_free(g_conf->receive_mq);
	memcacheq_st_free(g_conf->send_mq);
	redis_client_free(g_conf->redis_client);
	free(g_conf);
    }

}
size_t str_is_blank(const char *str)
{
    while (isspace(*str))
	str++;

    if (*str == 0) // All spaces?
    {
	return 1;
    }
    return 0;
}

size_t trimwhitespace(char *out, size_t len, const char *str)
{
    if (len == 0)
	return 0;

    const char *end;
    size_t out_size;

    // Trim leading space
    while (isspace(*str))
	str++;

    if (*str == 0) // All spaces?
    {
	*out = 0;
	return 1;
    }

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace(*end))
	end--;
    end++;

    // Set output size to minimum of trimmed string length and buffer size minus 1
    out_size = (end - str) < len - 1 ? (end - str) : len - 1;

    // Copy trimmed string and add null terminator
    memcpy(out, str, out_size);
    out[out_size] = 0;

    return out_size;
}

static int xode_get_integer(xode pxode, char *item)
{
    if (NULL == pxode || NULL == item)
    {
	return -1;
    }

    char *value = xode_get_tagdata(pxode, item);
    if (NULL == value)
    {
	return -1;
    }
    else
    {
	return atoi(value);
    }
}


static char *
xode_get_string(xode pxode, char *item)
{
    if (NULL == pxode || NULL == item)
    {
	return NULL;
    }

    char *value = xode_get_tagdata(pxode, item);

    return value;
}

/*static int isnumber(char *knum)
  {
  while (isdigit(*knum))
  knum++;
  if (*knum == '\0')
  return 0;
  return -1;
  }*/
static void get_addr_from_str(const char *addr, char **_ip, int*_port)
{
    char *ip = NULL;
    char *q = strchr(addr, ':');
    int port;
    if (NULL == q)
    {
	ip = strdup(addr);
	port = 11215;
	return;
    }
    *q = '\0';

    ip = strdup(addr);
    *q = ':';
    q++;
    port = atoi(q);
    if (port <= 0)
    {
	fprintf(stderr, "addr %s wrong for port\n", addr);
	exit(-1);
    }
    *_ip = ip;
    *_port = port;
    return;
}

/*----------------------------global_conf && mq setup------------------------*/
/**
 * 从字符串创建memcacheq的实例
 * params:
 *  addr: eg. 127.0.0.1:22201
 * return memcacheq instance
 **/
static memcached_st * create_memcacheq_from_str(const char *addr)
{
    if (addr == NULL)
	return NULL;
    memcached_return rc;
    memcached_st* ret = memcached_create(NULL);
    memcached_server_st *servers = NULL;
    int port;
    char *ip;
    get_addr_from_str(addr, &ip, &port);
    fprintf(stderr, "addr:port is %s-> %s:%d\n", addr, ip, port);
    servers = memcached_server_list_append(NULL, ip, port, &rc);
    free(ip);
    if (rc != MEMCACHED_SUCCESS)
    {
	memcached_free(ret);
	return NULL;
    }
    memcached_server_push(ret, servers);
    memcached_server_free(servers);
    return ret;
}

static memcached_pool_st* init_mq_pool(const char *addr, int max_pool)
{
    memcached_st *mq = create_memcacheq_from_str(addr);
    if (!mq)
    {
	printf("%s:%d create send pool error\n", __FILE__, __LINE__);
	exit(-1);
    }
    return memcached_pool_create(mq, max_pool, max_pool + 5);
}

static void set_default_conf(global_conf_st* g_conf)
{
    g_conf->max_thread = 10;
    g_conf->max_msg_queue_len = 200;
}

static void init_mq(global_conf_st *g_conf, xode pnode)
{

    if (g_conf == NULL && pnode == NULL)
    {
	exit(1);
    }
    xode receive_node = xode_get_tag(pnode, "receiveMq");
    if (receive_node == NULL)
    {
	exit(1);
    }
    memcacheq_st *mq_receive = NULL;
    mq_receive = (memcacheq_st *) calloc(sizeof(memcacheq_st), 1);
    char *task_name = NULL;
    char *addr = NULL;
    task_name = xode_get_tagdata(receive_node, "mqName");
    if (task_name == NULL)
    {
	fprintf(stderr, "task_name is needed\n");
	exit(1);
    }
    if (receive_type >= ERROR_TASK)
    {
	fprintf(stderr,
		"we can only accept task_name link_mining and advance_link_mining \n");
	exit(1);
    }
    if (NULL == (addr = xode_get_tagdata(receive_node, "mqAddr")))
    {
	fprintf(stderr, "mq addr is needed\n");
	exit(1);
    }
    snprintf(mq_receive->task_name, sizeof(mq_receive->task_name), "%s",
	     task_name);
    if (NULL == (mq_receive->mq_server = create_memcacheq_from_str(addr)))
    {
	fprintf(stderr, "%s:%d receive mq setup is wrong\n", __FILE__, __LINE__);
	exit(1);
    }
    mq_receive->mq_pool = NULL;
    g_conf->receive_mq = mq_receive;

    memcacheq_st *mq_send = NULL;
    xode send_node = xode_get_tag(pnode, "sendMq");
    if (send_node == NULL)
    {
	exit(1);
    }
    mq_send = (memcacheq_st *) calloc(sizeof(memcacheq_st), 1);
    task_name = xode_get_tagdata(send_node, "mqName");
    if (task_name == NULL)
    {
	fprintf(stderr, "task_name is needed\n");
	exit(1);
    }
    if (NULL == (addr = xode_get_tagdata(send_node, "mqAddr")))
    {
	fprintf(stderr, "mq addr is needed\n");
	exit(1);
    }

    snprintf(mq_send->task_name, sizeof(mq_send->task_name), "%s", task_name);
    mq_send->mq_server = NULL;
    mq_send->mq_pool = init_mq_pool(addr, g_conf->max_thread + 5);
    g_conf->send_mq = mq_send;
    return;
}

static int get_log_level(char *log_level)
{
    if (log_level == NULL)
	return L_DEBUG;
    if (strcmp(log_level, "L_DEBUG") == 0)
    {
	return L_DEBUG;
    }
    if (strcmp(log_level, "L_INFO") == 0)
    {
	return L_INFO;
    }
    if (strcmp(log_level, "L_ERR") == 0)
    {
	return L_ERR;
    }
    if (strcmp(log_level, "L_WARN") == 0)
    {
	return L_WARN;
    }
    if (strcmp(log_level, "L_CONS") == 0)
    {
	return L_CONS;
    }
    return L_DEBUG;
}

static void init_global_config(global_conf_st *g_conf, char *file)
{
    if (NULL == file)
    {
	fprintf(stderr, "config file is wrong\n");
	exit(1);
    }
    int L_level = 0;
    xode pnode = xode_from_file(file);
    if (NULL == pnode)
    {
	fprintf(stderr, "config file is wrong\n");
	exit(1);
    }
    int max_thread = xode_get_integer(pnode, "maxThread");
    if (max_thread != -1)
    {
	g_conf->max_thread = max_thread;
    }
    int max_msg_queue_len = xode_get_integer(pnode, "maxMsgQueueLen");
    if (max_msg_queue_len != -1)
    {
	g_conf->max_msg_queue_len = max_msg_queue_len;
    }
    char *log_file = xode_get_string(pnode, "logDir");
    char *redis_conf = xode_get_string(pnode, "redisConf");
    char *reload_file = xode_get_string(pnode, "reloadFile");
    if (log_file == NULL || redis_conf == NULL || reload_file == NULL)
    {
	fprintf(stderr,
		"conf file format wrong ,can't file logDir or redisConf or reload_file\n");
	exit(1);
    }

    snprintf(g_conf->log_dir, sizeof(g_conf->log_dir), "%s", log_file);
    snprintf(g_conf->reload_file, sizeof(g_conf->reload_file), "%s",
	     reload_file);
    snprintf(g_conf->redis_config, sizeof(g_conf->redis_config), "%s",
	     redis_conf);
    char *log_level = xode_get_string(pnode, "logLevel");
    if (log_level != NULL)
    {
	L_level = get_log_level(log_level);
    }
    char program[128];
    memset(program, 0, sizeof(program));
    init_mq(g_conf, pnode);
    snprintf(program, sizeof(program), "%s-%s", PROGRAME,
	     g_conf->receive_mq->task_name);

    gozap_log_init(g_conf->log_dir, L_level, program);

    g_conf->redis_client = redis_client_init(g_conf->redis_config);
    return;
}

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

static void cleanup(void *arg)
{
    pthread_mutex_lock(&init_lock);
    init_count--;
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);
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
	fprintf(stderr,"%d is runnning,one message processed\n",thread_info->ppid);
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

static int init_msg(msg_st *msg,char * msg_str)
{
    //TODO:transformat msg_str to msg structure
    return 0;
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


