#pragma once

#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/queue.h>
#include "pools.h"

#define HOSTLIST_AUTO_FAIL_THRESHOLD_DEFAULT	2
#define	HOSTLIST_AUTO_FAIL_TIME_DEFAULT 	60

enum splithttpreq { split_none = 0, split_method, split_host };
enum tlsrec { tlsrec_none = 0, tlsrec_sni, tlsrec_pos };
enum bindll { unwanted=0, no, prefer, force };

#define MAX_BINDS	32
struct bind_s
{
	char bindaddr[64],bindiface[IF_NAMESIZE];
	bool bind_if6;
	enum bindll bindll;
	int bind_wait_ifup,bind_wait_ip,bind_wait_ip_ll;
};

struct params_s
{
	struct bind_s binds[MAX_BINDS];
	int binds_last;
	bool bind_wait_only;
	uint16_t port;

	uint8_t proxy_type;
	bool no_resolve;
	bool skip_nodelay;
	bool droproot;
	uid_t uid;
	gid_t gid;
	bool daemon;
	int maxconn,maxfiles,max_orphan_time;
	int local_rcvbuf,local_sndbuf,remote_rcvbuf,remote_sndbuf;

	bool tamper; // any tamper option is set
	bool hostcase, hostdot, hosttab, hostnospace, methodspace, methodeol, unixeol, domcase;
	int hostpad;
	char hostspell[4];
	enum splithttpreq split_http_req;
	enum tlsrec tlsrec;
	int tlsrec_pos;
	bool split_any_protocol;
	int split_pos;
	bool disorder;
	int ttl_default;

	char pidfile[256];

	strpool *hostlist, *hostlist_exclude;
	struct str_list_head hostlist_files, hostlist_exclude_files;
	char hostlist_auto_filename[PATH_MAX];
	int hostlist_auto_fail_threshold, hostlist_auto_fail_time;
	hostfail_pool *hostlist_auto_fail_counters;

	int debug;

#if defined(BSD)
	bool pf_enable;
#endif
};

extern struct params_s params;

#define _DBGPRINT(format, level, ...) { if (params.debug>=level) printf(format "\n", ##__VA_ARGS__); }
#define VPRINT(format, ...) _DBGPRINT(format,1,##__VA_ARGS__)
#define DBGPRINT(format, ...) _DBGPRINT(format,2,##__VA_ARGS__)
