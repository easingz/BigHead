/*
 * link_mining.h
 *
 *  Created on: 2011-4-19
 *      Author: saint
 */

#ifndef LINK_MINING_H_
#define LINK_MINING_H_

#include <libmemcached/memcached.h>
#include <libmemcached/memcached_util.h>
#include "redis_client.h"
#include <pthread.h>
#include "xode.h"
#include "log.h"
#include <glib.h>
#define MAX_FILE_PATH_LENGTH 512


typedef struct _memcacheq_st
{
	char task_name[128];
	memcached_st *mq_server;
	memcached_pool_st *mq_pool;
} memcacheq_st;

typedef struct _global_conf_st
{
	memcacheq_st *receive_mq;
	memcacheq_st *send_mq;
	redis_client_st *redis_client;
	int max_thread;
	int max_msg_queue_len;
	char log_dir[MAX_FILE_PATH_LENGTH];
	char reload_file[MAX_FILE_PATH_LENGTH];
	char redis_config[MAX_FILE_PATH_LENGTH];
} global_conf_st;

typedef enum
{
	ACTION_ADD, ACTION_REM, ACTION_ERROR
} action_type;

typedef struct _msg_st
{
	char src_pnum[54];
	char dst_pnum[54];
	char guid[54];
	action_type action;
} msg_st;

typedef enum
{
	LINK_MINING, ADVANCE_LINK_MINGING, ERROR_TASK
} task_type_i;

typedef struct _work_thread
{
	global_conf_st *g_conf;
	GAsyncQueue *msg_queue;
	pthread_t ppid;
} work_thread;

typedef enum
{
	MSG_JID, MSG_PNUM, MSG_GUID,  MSG_ACTION, MSG_END
}MSG_ITEM;


void freeMsg(msg_st *msg);
void do_link_minging(global_conf_st* g_conf, msg_st *msg);




#endif /* LINK_MINING_H_ */
