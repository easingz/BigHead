/*
 * redis_link_api.h
 *
 *  Created on: 2011-4-1
 *      Author: saint
 */

#ifndef REDIS_LINK_API_H_
#define REDIS_LINK_API_H_
#include "redis_client.h"

//自动添加双向链接
redis_return_st add_conns(redis_client_st *redis_client, const char *jid, const char *conn_jid,
		const char *pnum, const char *guid);

redis_return_st add_following(redis_client_st *redis_client, const char *jid, const char*conn_jid,
		const char *pnum, const char *guid);

redis_return_st add_follower(redis_client_st *redis_client, const char *jid, const char* conn_jid);

redis_return_st del_conns(redis_client_st *redis_client, const char *jid, const char *conn_jid);

redis_return_st add_block(redis_client_st *redis_client, const char *jid, const char *conn_jid);

redis_return_st del_block(redis_client_st *redis_client, const char *jid, const char *conn_jid);
redis_return_st get_guids(redis_client_st *redis_client, const char *jid, const char *conn_jid,
		char ***guids, int *len);

//分組管理
redis_return_st add_jid_to_group(redis_client_st *redis_client, const char *jid,
		const char*conn_jid, const char *group);

redis_return_st del_group(redis_client_st *redis_client, const char *jid, const char *group);

redis_return_st del_jid_from_group(redis_client_st *redis_client, const char *jid,
		const char*conn_jid, const char *group);

#endif /* REDIS_LINK_API_H_ */
