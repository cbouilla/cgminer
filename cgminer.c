/*
 * Copyright 2011-2014 Con Kolivas
 * Copyright 2011-2012 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <assert.h>
#include <signal.h>
#include <limits.h>

#ifdef USE_USBUTILS
#include <semaphore.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <sys/resource.h>
#include <ccan/opt/opt.h>
#include <jansson.h>
char *curly = ":D";

#include <libgen.h>
#include <sha2.h>

#include "compat.h"
#include "miner.h"
#include "bench_block.h"
#ifdef USE_USBUTILS
#include "usbutils.h"
#endif


	#include <errno.h>
	#include <fcntl.h>
	#include <sys/wait.h>


#include "driver-bitmain.h"
#define USE_FPGA

#include <inttypes.h>

struct strategies strategies[] = {
	{ "Failover" },
	{ "Round Robin" },
	{ "Rotate" },
	{ "Load Balance" },
	{ "Balance" },
};

static char packagename[256];
static char *opt_set_null;

FILE * g_logwork_file = NULL;
FILE * g_logwork_files[65] = {0};
FILE * g_logwork_diffs[65] = {0};
int g_logwork_asicnum = 0;

bool opt_work_update;
bool opt_protocol;
static struct benchfile_layout {
	int length;
	char *name;
} benchfile_data[] = {
	{ 1,	"Version" },
	{ 64,	"MerkleRoot" },
	{ 64,	"PrevHash" },
	{ 8,	"DifficultyBits" },
	{ 10,	"NonceTime" } // 10 digits
};
enum benchwork {
	BENCHWORK_VERSION = 0,
	BENCHWORK_MERKLEROOT,
	BENCHWORK_PREVHASH,
	BENCHWORK_DIFFBITS,
	BENCHWORK_NONCETIME,
	BENCHWORK_COUNT
};

static char *opt_benchfile;
static bool opt_benchfile_display;
static FILE *benchfile_in;
static int benchfile_line;
static int benchfile_work;
static bool opt_benchmark;
bool have_longpoll;
bool want_per_device_stats;
bool use_syslog;
bool opt_quiet;
bool opt_realquiet;
bool opt_loginput;
bool opt_compact;
const int opt_cutofftemp = 95;
int opt_log_interval = 5;
int opt_queue = 1;
static int max_queue = 1;
int opt_scantime = -1;
int opt_expiry = 120;
unsigned long long global_hashrate;
unsigned long global_quota_gcd = 1;
time_t last_getwork;


int nDevs;
bool opt_restart = true;
bool opt_nogpu;

struct list_head scan_devices;
static bool opt_display_devs;
int total_devices;
int zombie_devs;
static int most_devices;
struct cgpu_info **devices;
int mining_threads;
int num_processors;

bool use_curses = false;

static bool opt_widescreen;
static bool alt_status;
static bool switch_status;
static bool opt_submit_stale = true;
static int opt_shares;
bool opt_fail_only;
static bool opt_fix_protocol;
bool opt_lowmem;
bool opt_autofan;
bool opt_autoengine;
bool opt_noadl;
char *opt_version_path = NULL;
char *opt_logfile_path = NULL;
char *opt_logfile_openflag = NULL;
char *opt_logwork_path = NULL;
char *opt_logwork_asicnum = NULL;
bool opt_logwork_diff = false;
char *opt_api_allow = NULL;
char *opt_api_groups;
char *opt_api_description = PACKAGE_STRING;
int opt_api_port = 4028;
char *opt_api_host = API_LISTEN_ADDR;
bool opt_api_listen;
bool opt_api_mcast;
char *opt_api_mcast_addr = API_MCAST_ADDR;
char *opt_api_mcast_code = API_MCAST_CODE;
char *opt_api_mcast_des = "";
int opt_api_mcast_port = 4028;
bool opt_api_network;
bool opt_delaynet;
bool opt_disable_pool;
static bool no_work;

bool opt_worktime;

#ifdef USE_BITMAIN
char *opt_bitmain_options = NULL;
char *opt_bitmain_freq = NULL;
char *opt_bitmain_voltage = NULL;
#endif

#ifdef USE_USBUTILS
char *opt_usb_select = NULL;
int opt_usbdump = -1;
bool opt_usb_list_all;
cgsem_t usb_resource_sem;
static pthread_t usb_poll_thread;
static bool usb_polling;
#endif

char *opt_kernel_path;
char *cgminer_path;

#define QUIET	(opt_quiet || opt_realquiet)

struct thr_info *control_thr;
struct thr_info **mining_thr;
static int gwsched_thr_id;
static int watchpool_thr_id;
static int watchdog_thr_id;

int gpur_thr_id;
static int api_thr_id;
#ifdef USE_USBUTILS
static int usbres_thr_id;
static int hotplug_thr_id;
#endif
static int total_control_threads;
bool hotplug_mode;
static int new_devices;
static int new_threads;
int hotplug_time = 5;

#if LOCK_TRACKING
pthread_mutex_t lockstat_lock;
#endif

pthread_mutex_t hash_lock;
static pthread_mutex_t *stgd_lock;
pthread_mutex_t console_lock;
cglock_t ch_lock;
static pthread_rwlock_t blk_lock;
static pthread_mutex_t sshare_lock;

pthread_rwlock_t netacc_lock;
pthread_rwlock_t mining_thr_lock;
pthread_rwlock_t devices_lock;

static pthread_mutex_t lp_lock;
static pthread_cond_t lp_cond;

pthread_mutex_t restart_lock;
pthread_cond_t restart_cond;

pthread_cond_t gws_cond;

#define CG_LOCAL_MHASHES_MAX_NUM 12
double g_local_mhashes_dones[CG_LOCAL_MHASHES_MAX_NUM] = {0};
int g_local_mhashes_index = 0;
double g_displayed_rolling = 0;
char g_miner_version[256] = {0};
char g_miner_compiletime[256] = {0};
char g_miner_type[256] = {0};

double rolling1, rolling5, rolling15;
double total_rolling;
double total_mhashes_done;
static struct timeval total_tv_start, total_tv_end;
static struct timeval restart_tv_start, update_tv_start;

cglock_t control_lock;
pthread_mutex_t stats_lock;

int hw_errors;
int g_max_fan, g_max_temp;
int64_t total_accepted, total_rejected, total_diff1;
int64_t total_getworks, total_stale, total_discarded;
double total_diff_accepted, total_diff_rejected, total_diff_stale;
static int staged_rollable;
unsigned int new_blocks;
static unsigned int work_block = 0;
unsigned int found_blocks;

unsigned int local_work;
unsigned int local_work_last = 0;
long local_work_lasttime = 0;
unsigned int total_go, total_ro;

struct pool **pools;
static struct pool *currentpool = NULL;

int total_pools, enabled_pools;
enum pool_strategy pool_strategy = POOL_FAILOVER;
int opt_rotate_period;
// static int total_urls, total_users, total_passes, total_userpasses;

static
#ifndef HAVE_CURSES
const
#endif
bool curses_active;

/* Protected by ch_lock */
char current_hash[68];
static char prev_block[12];
static char current_block[32];

static char datestamp[40];
static char blocktime[32];
struct timeval block_timeval;
static char best_share[8] = "0";
double current_diff = 0xFFFFFFFFFFFFFFFFULL;
static char block_diff[8];
uint64_t best_diff = 0;

struct block {
	char hash[68];
	UT_hash_handle hh;
	int block_no;
};

static struct block *blocks = NULL;


int swork_id;

/* For creating a hash database of stratum shares submitted that have not had
 * a response yet */
struct stratum_share {
	UT_hash_handle hh;
	bool block;
	struct work *work;
	int id;
	time_t sshare_time;
	time_t sshare_sent;
};

static struct stratum_share *stratum_shares = NULL;

char *opt_socks_proxy = NULL;
int opt_suggest_diff;
static const char def_conf[] = "cgminer.conf";
static char *default_config;
static bool config_loaded;
static int include_count;
#define JSON_INCLUDE_CONF "include"
#define JSON_LOAD_ERROR "JSON decode of file '%s' failed\n %s"
#define JSON_LOAD_ERROR_LEN strlen(JSON_LOAD_ERROR)
#define JSON_MAX_DEPTH 10
#define JSON_MAX_DEPTH_ERR "Too many levels of JSON includes (limit 10) or a loop"
#define JSON_WEB_ERROR "WEB config err"

static int forkpid;


struct sigaction termhandler, inthandler;

struct thread_q *getq;

static uint32_t total_work;
struct work *staged_work = NULL;

struct schedtime {
	bool enable;
	struct tm tm;
};

struct schedtime schedstart;
struct schedtime schedstop;
bool sched_paused;

static bool time_before(struct tm *tm1, struct tm *tm2)
{
	if (tm1->tm_hour < tm2->tm_hour)
		return true;
	if (tm1->tm_hour == tm2->tm_hour && tm1->tm_min < tm2->tm_min)
		return true;
	return false;
}

static bool should_run(void)
{
	struct timeval tv;
	struct tm *tm;

	if (!schedstart.enable && !schedstop.enable)
		return true;

	cgtime(&tv);
	const time_t tmp_time = tv.tv_sec;
	tm = localtime(&tmp_time);
	if (schedstart.enable) {
		if (!schedstop.enable) {
			if (time_before(tm, &schedstart.tm))
				return false;

			/* This is a once off event with no stop time set */
			schedstart.enable = false;
			return true;
		}
		if (time_before(&schedstart.tm, &schedstop.tm)) {
			if (time_before(tm, &schedstop.tm) && !time_before(tm, &schedstart.tm))
				return true;
			return false;
		} /* Times are reversed */
		if (time_before(tm, &schedstart.tm)) {
			if (time_before(tm, &schedstop.tm))
				return true;
			return false;
		}
		return true;
	}
	/* only schedstop.enable == true */
	if (!time_before(tm, &schedstop.tm))
		return false;
	return true;
}

void get_datestamp(char *f, size_t fsiz, struct timeval *tv)
{
	struct tm *tm;

	const time_t tmp_time = tv->tv_sec;
	tm = localtime(&tmp_time);
	snprintf(f, fsiz, "[%d-%02d-%02d %02d:%02d:%02d]",
		tm->tm_year + 1900,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static void get_timestamp(char *f, size_t fsiz, struct timeval *tv)
{
	struct tm *tm;

	const time_t tmp_time = tv->tv_sec;
	tm = localtime(&tmp_time);
	snprintf(f, fsiz, "[%02d:%02d:%02d]",
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec);
}

static char exit_buf[512];

static void applog_and_exit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(exit_buf, sizeof(exit_buf), fmt, ap);
	va_end(ap);
	_applog(LOG_ERR, exit_buf, true);
	exit(1);
}

static pthread_mutex_t sharelog_lock;
static FILE *sharelog_file = NULL;

static struct thr_info *__get_thread(int thr_id)
{
	return mining_thr[thr_id];
}

struct thr_info *get_thread(int thr_id)
{
	struct thr_info *thr;

	rd_lock(&mining_thr_lock);
	thr = __get_thread(thr_id);
	rd_unlock(&mining_thr_lock);

	return thr;
}

static struct cgpu_info *get_thr_cgpu(int thr_id)
{
	struct thr_info *thr = get_thread(thr_id);

	return thr->cgpu;
}

struct cgpu_info *get_devices(int id)
{
	struct cgpu_info *cgpu;

	rd_lock(&devices_lock);
	cgpu = devices[id];
	rd_unlock(&devices_lock);

	return cgpu;
}

static void sharelog(const char*disposition, const struct work*work)
{
	char *target, *hash, *data;
	struct cgpu_info *cgpu;
	unsigned long int t;
	struct pool *pool;
	int thr_id, rv;
	char s[1024];
	size_t ret;

	if (!sharelog_file)
		return;

	thr_id = work->thr_id;
	cgpu = get_thr_cgpu(thr_id);
	pool = work->pool;
	t = (unsigned long int)(work->tv_work_found.tv_sec);
	target = bin2hex(work->target, sizeof(work->target));
	hash = bin2hex(work->hash, sizeof(work->hash));
	data = bin2hex(work->data, sizeof(work->data));

	// timestamp,disposition,target,pool,dev,thr,sharehash,sharedata
	rv = snprintf(s, sizeof(s), "%lu,%s,%s,%s,%s%u,%u,%s,%s\n", t, disposition, target, pool->rpc_url, cgpu->drv->name, cgpu->device_id, thr_id, hash, data);
	free(target);
	free(hash);
	free(data);
	if (rv >= (int)(sizeof(s)))
		s[sizeof(s) - 1] = '\0';
	else if (rv < 0) {
		applog(LOG_ERR, "sharelog printf error");
		return;
	}

	mutex_lock(&sharelog_lock);
	ret = fwrite(s, rv, 1, sharelog_file);
	fflush(sharelog_file);
	mutex_unlock(&sharelog_lock);

	if (ret != 1)
		applog(LOG_ERR, "sharelog fwrite error");
}

static char *getwork_req = "{\"method\": \"getwork\", \"params\": [], \"id\":0}\n";

static char *gbt_req = "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": [{\"capabilities\": [\"coinbasetxn\", \"workid\", \"coinbase/append\"]}]}\n";

static char *gbt_solo_req = "{\"id\": 0, \"method\": \"getblocktemplate\"}\n";

/* Adjust all the pools' quota to the greatest common denominator after a pool
 * has been added or the quotas changed. */
void adjust_quota_gcd(void)
{
	unsigned long gcd, lowest_quota = ~0UL, quota;
	struct pool *pool;
	int i;

	for (i = 0; i < total_pools; i++) {
		pool = pools[i];
		quota = pool->quota;
		if (!quota)
			continue;
		if (quota < lowest_quota)
			lowest_quota = quota;
	}

	if (likely(lowest_quota < ~0UL)) {
		gcd = lowest_quota;
		for (i = 0; i < total_pools; i++) {
			pool = pools[i];
			quota = pool->quota;
			if (!quota)
				continue;
			while (quota % gcd)
				gcd--;
		}
	} else
		gcd = 1;

	for (i = 0; i < total_pools; i++) {
		pool = pools[i];
		pool->quota_used *= global_quota_gcd;
		pool->quota_used /= gcd;
		pool->quota_gcd = pool->quota / gcd;
	}

	global_quota_gcd = gcd;
	applog(LOG_DEBUG, "Global quota greatest common denominator set to %lu", gcd);
}

/* Return value is ignored if not called from add_pool_details */
struct pool *add_pool(void)
{
	struct pool *pool;

	pool = calloc(sizeof(struct pool), 1);
	if (!pool)
		quit(1, "Failed to malloc pool in add_pool");
	pool->pool_no = pool->prio = total_pools;
	pools = realloc(pools, sizeof(struct pool *) * (total_pools + 2));
	pools[total_pools++] = pool;
	mutex_init(&pool->pool_lock);
	if (unlikely(pthread_cond_init(&pool->cr_cond, NULL)))
		quit(1, "Failed to pthread_cond_init in add_pool");
	cglock_init(&pool->data_lock);
	mutex_init(&pool->stratum_lock);
	cglock_init(&pool->gbt_lock);
	INIT_LIST_HEAD(&pool->curlring);

	/* Make sure the pool doesn't think we've been idle since time 0 */
	pool->tv_idle.tv_sec = ~0UL;

	pool->rpc_req = getwork_req;
	pool->rpc_proxy = NULL;
	pool->quota = 1;
	adjust_quota_gcd();

	return pool;
}

/* Pool variant of test and set */
static bool pool_tset(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = true;
	mutex_unlock(&pool->pool_lock);

	return ret;
}

bool pool_tclear(struct pool *pool, bool *var)
{
	bool ret;

	mutex_lock(&pool->pool_lock);
	ret = *var;
	*var = false;
	mutex_unlock(&pool->pool_lock);

	return ret;
}

struct pool *current_pool(void)
{
	struct pool *pool;

	cg_rlock(&control_lock);
	pool = currentpool;
	cg_runlock(&control_lock);

	return pool;
}

char *set_int_range(const char *arg, int *i, int min, int max)
{
	char *err = opt_set_intval(arg, i);

	if (err)
		return err;

	if (*i < min || *i > max)
		return "Value out of range";

	return NULL;
}

static char *set_int_0_to_9999(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 9999);
}

static char *set_int_1_to_65535(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 65535);
}

static char *set_int_0_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 10);
}

static char *set_int_0_to_100(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 100);
}

static char *set_int_0_to_255(const char *arg, int *i)
{
        return set_int_range(arg, i, 0, 255);
}

static char *set_int_0_to_200(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 200);
}

static char *set_int_32_to_63(const char *arg, int *i)
{
	return set_int_range(arg, i, 32, 63);
}

static char *set_int_1_to_10(const char *arg, int *i)
{
	return set_int_range(arg, i, 1, 10);
}

static char __maybe_unused *set_int_0_to_4(const char *arg, int *i)
{
	return set_int_range(arg, i, 0, 4);
}

#ifdef USE_FPGA_SERIAL
static char *opt_add_serial;
static char *add_serial(char *arg)
{
	string_elist_add(arg, &scan_devices);
	return NULL;
}
#endif

void get_intrange(char *arg, int *val1, int *val2)
{
	if (sscanf(arg, "%d-%d", val1, val2) == 1)
		*val2 = *val1;
}


/* Detect that url is for a stratum protocol either via the presence of
 * stratum+tcp or by detecting a stratum server response */
bool detect_stratum(struct pool *pool, char *url)
{
	check_extranonce_option(pool, url);
	if (!extract_sockaddr(url, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	if (!strncasecmp(url, "stratum+tcp://", 14)) {
		pool->rpc_url = strdup(url);
		pool->has_stratum = true;
		pool->stratum_url = pool->sockaddr_url;
		return true;
	}

	return false;
}



static char *enable_debug(bool *flag)
{
	*flag = true;
	/* Turn on verbose output, too. */
	opt_log_output = true;
	return NULL;
}

static char *temp_cutoff_str = NULL;
static char __maybe_unused *opt_set_temp_cutoff;

char *set_temp_cutoff(char *arg)
{
	int val;

	if (!(arg && arg[0]))
		return "Invalid parameters for set temp cutoff";
	val = atoi(arg);
	if (val < 0 || val > 200)
		return "Invalid value passed to set temp cutoff";
	temp_cutoff_str = arg;

	return NULL;
}

static void load_temp_cutoffs()
{
	int i, val = 0, device = 0;
	char *nextptr;

	if (temp_cutoff_str) {
		for (device = 0, nextptr = strtok(temp_cutoff_str, ","); nextptr; ++device, nextptr = strtok(NULL, ",")) {
			if (device >= total_devices)
				quit(1, "Too many values passed to set temp cutoff");
			val = atoi(nextptr);
			if (val < 0 || val > 200)
				quit(1, "Invalid value passed to set temp cutoff");

			rd_lock(&devices_lock);
			devices[device]->cutofftemp = val;
			rd_unlock(&devices_lock);
		}
	} else {
		rd_lock(&devices_lock);
		for (i = device; i < total_devices; ++i) {
			if (!devices[i]->cutofftemp)
				devices[i]->cutofftemp = opt_cutofftemp;
		}
		rd_unlock(&devices_lock);

		return;
	}
	if (device <= 1) {
		rd_lock(&devices_lock);
		for (i = device; i < total_devices; ++i)
			devices[i]->cutofftemp = val;
		rd_unlock(&devices_lock);
	}
}

static char *set_logfile_path(const char *arg)
{
	opt_set_charp(arg, &opt_logfile_path);

	return NULL;
}

static char *set_float_125_to_500(const char *arg, float *i)
{
	char *err = opt_set_floatval(arg, i);

	if (err)
		return err;

	if (*i < 125 || *i > 500)
		return "Value out of range";

	return NULL;
}

static char *set_float_100_to_250(const char *arg, float *i)
{
	char *err = opt_set_floatval(arg, i);

	if (err)
		return err;

	if (*i < 100 || *i > 250)
		return "Value out of range";

	return NULL;
}


static char *set_bitmain_options(const char *arg)
{
	opt_set_charp(arg, &opt_bitmain_options);

	return NULL;
}
static char *set_bitmain_freq(const char *arg)
{
	opt_set_charp(arg, &opt_bitmain_freq);

	return NULL;
}
static char *set_bitmain_voltage(const char *arg)
{
	opt_set_charp(arg, &opt_bitmain_voltage);

	return NULL;
}



static char *set_null(const char __maybe_unused *arg)
{
	return NULL;
}

/* These options are available from config file or commandline */
static struct opt_table opt_config_table[] = {


	// OPT_WITH_ARG("--version-file",
	// 		 set_version_path, NULL, opt_hidden,
	// 	     "Set miner version file"),
	// OPT_WITH_ARG("--logfile-openflag",
	// 		set_logfile_openflag, NULL, opt_hidden,
	// 		"Set log file open flag, default: a+"),
	// OPT_WITH_ARG("--logwork",
	// 		set_logwork_path, NULL, opt_hidden,
	// 		"Set log work file path, following: minertext"),
	// OPT_WITH_ARG("--logwork-asicnum",
	// 		set_logwork_asicnum, NULL, opt_hidden,
	// 		"Set log work asic num, following: 1, 32, 64"),
	// OPT_WITHOUT_ARG("--logwork-diff",
	// 		opt_set_bool, &opt_logwork_diff,
	// 		"Allow log work diff"),
	OPT_WITH_ARG("--logfile",
				set_logfile_path, NULL, opt_hidden,
				"Set log file, default: cgminer.log"),	
	// OPT_WITH_ARG("--api-allow",
	// 	     opt_set_charp, NULL, &opt_api_allow,
	// 	     "Allow API access only to the given list of [G:]IP[/Prefix] addresses[/subnets]"),
	// OPT_WITH_ARG("--api-description",
	// 	     opt_set_charp, NULL, &opt_api_description,
	// 	     "Description placed in the API status header, default: cgminer version"),
	// OPT_WITH_ARG("--api-groups",
	// 	     opt_set_charp, NULL, &opt_api_groups,
	// 	     "API one letter groups G:cmd:cmd[,P:cmd:*...] defining the cmds a groups can use"),
	// OPT_WITHOUT_ARG("--api-listen",
	// 		opt_set_bool, &opt_api_listen,
	// 		"Enable API, default: disabled"),
	// OPT_WITHOUT_ARG("--api-mcast",
	// 		opt_set_bool, &opt_api_mcast,
	// 		"Enable API Multicast listener, default: disabled"),
	// OPT_WITH_ARG("--api-mcast-addr",
	// 	     opt_set_charp, NULL, &opt_api_mcast_addr,
	// 	     "API Multicast listen address"),
	// OPT_WITH_ARG("--api-mcast-code",
	// 	     opt_set_charp, NULL, &opt_api_mcast_code,
	// 	     "Code expected in the API Multicast message, don't use '-'"),
	// OPT_WITH_ARG("--api-mcast-des",
	// 	     opt_set_charp, NULL, &opt_api_mcast_des,
	// 	     "Description appended to the API Multicast reply, default: ''"),
	// OPT_WITH_ARG("--api-mcast-port",
	// 	     set_int_1_to_65535, opt_show_intval, &opt_api_mcast_port,
	// 	     "API Multicast listen port"),
	// OPT_WITHOUT_ARG("--api-network",
	// 		opt_set_bool, &opt_api_network,
	// 		"Allow API (if enabled) to listen on/for any address, default: only 127.0.0.1"),
	// OPT_WITH_ARG("--api-port",
	// 	     set_int_1_to_65535, opt_show_intval, &opt_api_port,
	// 	     "Port number of miner API"),
	// OPT_WITH_ARG("--api-host",
	// 	     opt_set_charp, NULL, &opt_api_host,
	// 	     "Specify API listen address, default: 0.0.0.0"),

// bitmain stuff
	OPT_WITH_ARG("--bitmain-dev",
			set_bitmain_dev, NULL, NULL,
			"Set bitmain device (default: usb mode, other windows: COM1 or linux: /dev/bitmain-asic)"),
	OPT_WITHOUT_ARG("--bitmain-hwerror",
			opt_set_bool, &opt_bitmain_hwerror,
			"Set bitmain device detect hardware error"),
	OPT_WITHOUT_ARG("--bitmain-checkall",
			opt_set_bool, &opt_bitmain_checkall,
			"Set bitmain check all"),
	OPT_WITHOUT_ARG("--bitmain-checkn2diff",
			opt_set_bool, &opt_bitmain_checkn2diff,
			"Set bitmain check not 2 pow diff"),
	OPT_WITHOUT_ARG("--bitmain-nobeeper",
			opt_set_bool, &opt_bitmain_nobeeper,
			"Set bitmain beeper no ringing"),
	OPT_WITHOUT_ARG("--bitmain-notempoverctrl",
			opt_set_bool, &opt_bitmain_notempoverctrl,
			"Set bitmain not stop runing when temprerature is over 80 degree Celsius"),
	OPT_WITHOUT_ARG("--bitmain-auto",
			opt_set_bool, &opt_bitmain_auto,
			"Adjust bitmain overclock frequency dynamically for best hashrate"),
	OPT_WITHOUT_ARG("--bitmain-homemode",
			opt_set_bool, &opt_bitmain_homemode,
			"Set bitmain miner to home mode"),
	OPT_WITHOUT_ARG("--bitmain-use-vil",
			opt_set_bool, &opt_bitmain_new_cmd_type_vil,
			"Set bitmain miner use vil mode"),
	OPT_WITH_ARG("--bitmain-cutoff",
		     set_int_0_to_100, opt_show_intval, &opt_bitmain_overheat,
		     "Set bitmain overheat cut off temperature"),
	OPT_WITH_ARG("--bitmain-fan",
		     set_bitmain_fan, NULL, NULL,
		     "Set fanspeed percentage for bitmain, single value or range (default: 20-100)"),
	OPT_WITH_ARG("--bitmain-freq",
		     set_bitmain_freq, NULL, NULL,
		     "Set frequency"),
	OPT_WITH_ARG("--bitmain-voltage",
		     set_bitmain_voltage, NULL, NULL,
		     "Set voltage"),
	OPT_WITH_ARG("--bitmain-options",
		     set_bitmain_options, NULL, NULL,
		     "Set bitmain options baud:miners:asic:timeout:freq"),
	OPT_WITH_ARG("--bitmain-temp",
		     set_int_0_to_100, opt_show_intval, &opt_bitmain_temp,
		     "Set bitmain target temperature"),


	// OPT_WITH_ARG("--hotplug",
	// 	     set_int_0_to_9999, NULL, &hotplug_time,
	// 	     "Seconds between hotplug checks (0 means never check)"
	// 	    ),


	// OPT_WITH_ARG("--monitor|-m",
	// 	     opt_set_charp, NULL, &opt_stderr_cmd,
	// 	     "Use custom pipe cmd for output messages"),

	// OPT_WITHOUT_ARG("--net-delay",
	// 		opt_set_bool, &opt_delaynet,
	// 		"Impose small delays in networking to not overload slow routers"),
	// OPT_WITHOUT_ARG("--no-pool-disable",
	// 		opt_set_invbool, &opt_disable_pool,
	// 		opt_hidden),
	// OPT_WITHOUT_ARG("--no-submit-stale",
	// 		opt_set_invbool, &opt_submit_stale,
	// 	        "Don't submit shares if they are detected as stale"),

	// OPT_WITH_ARG("--pass|-p",
	// 	     set_pass, NULL, &opt_set_null,
	// 	     "Password for bitcoin JSON-RPC server"),
	// OPT_WITHOUT_ARG("--per-device-stats",
	//		opt_set_bool, &want_per_device_stats,
	// 		"Force verbose mode and output per-device statistics"),
	// OPT_WITH_ARG("--pools",
	// 		opt_set_bool, NULL, &opt_set_null, opt_hidden),
	// OPT_WITHOUT_ARG("--protocol-dump|-P",
	// 		opt_set_bool, &opt_protocol,
	// 		"Verbose dump of protocol-level activities"),
	OPT_WITH_ARG("--queue|-Q",
		     set_int_0_to_9999, opt_show_intval, &opt_queue,
		     "Maximum number of work items to have queued"),
	OPT_WITHOUT_ARG("--quiet|-q",
			opt_set_bool, &opt_quiet,
			"Disable logging output, display status and errors"),
	// OPT_WITH_ARG("--quota|-U",
	// 	     set_quota, NULL, &opt_set_null,
	// 	     "quota;URL combination for server with load-balance strategy quotas"),
	OPT_WITHOUT_ARG("--real-quiet",
			opt_set_bool, &opt_realquiet,
			"Disable all output"),
	// OPT_WITH_ARG("--retries",
	// 	     set_null, NULL, &opt_set_null,
	// 	     opt_hidden),
	// OPT_WITH_ARG("--retry-pause",
	// 	     set_null, NULL, &opt_set_null,
	// 	     opt_hidden),
	// OPT_WITH_ARG("--rotate",
	// 	     set_rotate, NULL, &opt_set_null,
	// 	     "Change multipool strategy from failover to regularly rotate at N minutes"),
	// OPT_WITHOUT_ARG("--round-robin",
	// 	     set_rr, &pool_strategy,
	// 	     "Change multipool strategy from failover to round robin on failure"),
// #ifdef USE_FPGA_SERIAL
// 	OPT_WITH_CBARG("--scan-serial|-S",
// 		     add_serial, NULL, &opt_add_serial,
// 		     "Serial port to probe for Serial FPGA Mining device"),
// #endif
// 	OPT_WITH_ARG("--scan-time|-s",
// 		     set_int_0_to_9999, opt_show_intval, &opt_scantime,
// 		     "Upper bound on time spent scanning current work, in seconds"),
// 	OPT_WITH_CBARG("--sched-start",
// 		     set_sched_start, NULL, &opt_set_sched_start,
// 		     "Set a time of day in HH:MM to start mining (a once off without a stop time)"),
// 	OPT_WITH_CBARG("--sched-stop",
// 		     set_sched_stop, NULL, &opt_set_sched_stop,
// 		     "Set a time of day in HH:MM to stop mining (will quit without a start time)"),
// 	OPT_WITH_CBARG("--sharelog",
// 		     set_sharelog, NULL, &opt_set_sharelog,
// 		     "Append share log to file"),
// 	OPT_WITH_ARG("--shares",
// 		     opt_set_intval, NULL, &opt_shares,
// 		     "Quit after mining N shares (default: unlimited)"),
// 	OPT_WITH_ARG("--socks-proxy",
// 		     opt_set_charp, NULL, &opt_socks_proxy,
// 		     "Set socks4 proxy (host:port)"),
// 	OPT_WITH_ARG("--suggest-diff",
// 		     opt_set_intval, NULL, &opt_suggest_diff,
// 		     "Suggest miner difficulty for pool to user (default: none)"),
// #ifdef HAVE_SYSLOG_H
// 	OPT_WITHOUT_ARG("--syslog",
// 			opt_set_bool, &use_syslog,
// 			"Use system log for output messages (default: standard error)"),
// #endif
	OPT_WITHOUT_ARG("--text-only|-T",
			opt_set_invbool, &use_curses,
			opt_hidden
	),
	// OPT_WITH_ARG("--url|-o",
	// 	     set_url, NULL, &opt_set_null,
	// 	     "URL for bitcoin JSON-RPC server"),
// #ifdef USE_USBUTILS
// 	OPT_WITH_ARG("--usb",
// 		     opt_set_charp, NULL, &opt_usb_select,
// 		     "USB device selection"),
// 	OPT_WITH_ARG("--usb-dump",
// 		     set_int_0_to_10, opt_show_intval, &opt_usbdump,
// 		     opt_hidden),
// 	OPT_WITHOUT_ARG("--usb-list-all",
// 			opt_set_bool, &opt_usb_list_all,
// 			opt_hidden),
// #endif
// 	OPT_WITH_ARG("--user|-u",
// 		     set_user, NULL, &opt_set_null,
// 		     "Username for bitcoin JSON-RPC server"),
// 	OPT_WITH_ARG("--userpass|-O",
// 		     set_userpass, NULL, &opt_set_null,
// 		     "Username:Password pair for bitcoin JSON-RPC server"),
	OPT_WITHOUT_ARG("--verbose",
			opt_set_bool, &opt_log_output,
			"Log verbose output to stderr as well as status output"),
	OPT_WITHOUT_ARG("--widescreen",
			opt_set_bool, &opt_widescreen,
			"Use extra wide display without toggling"),
	OPT_WITHOUT_ARG("--worktime",
			opt_set_bool, &opt_worktime,
			"Display extra work time debug information"),
	OPT_ENDTABLE
};

static char *load_config(const char *arg, void __maybe_unused *unused);

static int fileconf_load;

static char *parse_config(json_t *config, bool fileconf)
{
	static char err_buf[200];
	struct opt_table *opt;
	const char *str;
	json_t *val;

	if (fileconf && !fileconf_load)
		fileconf_load = 1;

	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name;

		/* We don't handle subtables. */
		assert(!(opt->type & OPT_SUBTABLE));

		if (!opt->names)
			continue;

		/* Pull apart the option name(s). */
		name = strdup(opt->names);
		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			char *err = NULL;

			/* Ignore short options. */
			if (p[1] != '-')
				continue;

			val = json_object_get(config, p+2);
			if (!val)
				continue;

			if ((opt->type & (OPT_HASARG | OPT_PROCESSARG)) && json_is_string(val)) {
				str = json_string_value(val);
				err = opt->cb_arg(str, opt->u.arg);
				if (opt->type == OPT_PROCESSARG)
					opt_set_charp(str, opt->u.arg);
			} else if ((opt->type & (OPT_HASARG | OPT_PROCESSARG)) && json_is_array(val)) {
				json_t *arr_val;
				size_t index;

				json_array_foreach(val, index, arr_val) {
					if (json_is_string(arr_val)) {
						str = json_string_value(arr_val);
						err = opt->cb_arg(str, opt->u.arg);
						if (opt->type == OPT_PROCESSARG)
							opt_set_charp(str, opt->u.arg);
					} else if (json_is_object(arr_val))
						err = parse_config(arr_val, false);
					if (err)
						break;
				}
			} else if ((opt->type & OPT_NOARG) && json_is_true(val))
				err = opt->cb(opt->u.arg);
			else
				err = "Invalid value";

			if (err) {
				/* Allow invalid values to be in configuration
				 * file, just skipping over them provided the
				 * JSON is still valid after that. */
				if (fileconf) {
					applog(LOG_ERR, "Invalid config option %s: %s", p, err);
					fileconf_load = -1;
				} else {
					snprintf(err_buf, sizeof(err_buf), "Parsing JSON option %s: %s",
						p, err);
					return err_buf;
				}
			}
		}
		free(name);
	}

	val = json_object_get(config, JSON_INCLUDE_CONF);
	if (val && json_is_string(val))
		return load_config(json_string_value(val), NULL);

	return NULL;
}

char *cnfbuf = NULL;

#ifdef HAVE_LIBCURL
char conf_web1[] = "http://";
char conf_web2[] = "https://";

static char *load_web_config(const char *arg)
{
	json_t *val = json_web_config(arg);

	if (!val || !json_is_object(val))
		return JSON_WEB_ERROR;

	if (!cnfbuf)
		cnfbuf = strdup(arg);

	config_loaded = true;

	return parse_config(val, true);
}
#endif

static char *load_config(const char *arg, void __maybe_unused *unused)
{
	json_error_t err;
	json_t *config;
	char *json_error;
	size_t siz;

#ifdef HAVE_LIBCURL
	if (strncasecmp(arg, conf_web1, sizeof(conf_web1)-1) == 0 ||
	    strncasecmp(arg, conf_web2, sizeof(conf_web2)-1) == 0)
		return load_web_config(arg);
#endif

	if (!cnfbuf)
		cnfbuf = strdup(arg);

	if (++include_count > JSON_MAX_DEPTH)
		return JSON_MAX_DEPTH_ERR;

	config = json_load_file(arg, 0, &err);
	if (!json_is_object(config)) {
		siz = JSON_LOAD_ERROR_LEN + strlen(arg) + strlen(err.text);
		json_error = malloc(siz);
		if (!json_error)
			quit(1, "Malloc failure in json error");

		snprintf(json_error, siz, JSON_LOAD_ERROR, arg, err.text);
		return json_error;
	}

	config_loaded = true;

	/* Parse the config now, so we can override it.  That can keep pointers
	 * so don't free config object. */
	return parse_config(config, true);
}


void default_save_file(char *filename);

static void load_default_config(void)
{
	cnfbuf = malloc(PATH_MAX);

	default_save_file(cnfbuf);

	if (!access(cnfbuf, R_OK))
		load_config(cnfbuf, NULL);
	else {
		free(cnfbuf);
		cnfbuf = NULL;
	}
}

extern const char *opt_argv0;

static char *opt_verusage_and_exit(const char *extra)
{
	printf("%s\nCustom Built by C. Bouillaguet for Bitmain miners\n", packagename);
	printf("%s", opt_usage(opt_argv0, extra));
	fflush(stdout);
	exit(0);
}

char *display_devs(int *ndevs)
{
	*ndevs = 0;
	usb_all(0);
	exit(*ndevs);
}


/* These options are available from commandline only */
static struct opt_table opt_cmdline_table[] = {
	// OPT_WITH_ARG("--config|-c",
	// 	     load_config, NULL, &opt_set_null,
	// 	     "Load a JSON-format configuration file\n"
	// 	     "See example.conf for an example configuration."),
	// OPT_WITH_ARG("--default-config",
	// 	     set_default_config, NULL, &opt_set_null,
	// 	     "Specify the filename of the default config file\n"
	// 	     "Loaded at start and used when saving without a name."),
	OPT_WITHOUT_ARG("--help|-h",
			opt_verusage_and_exit, NULL,
			"Print this message"),
// #if defined(USE_USBUTILS)
// 	OPT_WITHOUT_ARG("--ndevs|-n",
// 			display_devs, &nDevs,
// 			"Display all USB devices and exit"),
// #endif
	OPT_WITHOUT_ARG("--version|-V",
			opt_version_and_exit, packagename,
			"Display version and exit"),
	OPT_ENDTABLE
};



static void calc_midstate(struct work *work)
{
	unsigned char data[64];
	uint32_t *data32 = (uint32_t *)data;
	sha256_ctx ctx;

	flip64(data32, work->data);
	sha256_init(&ctx);
	sha256_update(&ctx, data, 64);
	memcpy(work->midstate, ctx.h, 32);
	endian_flip32(work->midstate, work->midstate);
}

/* Returns the current value of total_work and increments it */
static int total_work_inc(void)
{
	int ret;

	cg_wlock(&control_lock);
	ret = total_work++;
	cg_wunlock(&control_lock);

	return ret;
}

static struct work *make_work(void)
{
	struct work *work = calloc(1, sizeof(struct work));

	if (unlikely(!work))
		quit(1, "Failed to calloc work in make_work");

	work->id = total_work_inc();

	return work;
}

/* This is the central place all work that is about to be retired should be
 * cleaned to remove any dynamically allocated arrays within the struct */
void clean_work(struct work *work)
{
	free(work->job_id);
	free(work->ntime);
	free(work->coinbase);
	free(work->nonce1);
	memset(work, 0, sizeof(struct work));
}

/* All dynamically allocated work structs should be freed here to not leak any
 * ram from arrays allocated within the work struct */
void _free_work(struct work *work)
{
	clean_work(work);
	free(work);
}

static void gen_hash(unsigned char *data, unsigned char *hash, int len);
static void calc_diff(struct work *work, double known);
char *workpadding = "000000800000000000000000000000000000000000000000000000000000000000000000000000000000000080020000";


/* Always true with stratum */
#define pool_localgen(pool) (true)
#define json_rpc_call(curl, url, userpass, rpc_req, probe, longpoll, rolltime, pool, share) (NULL)
#define work_decode(pool, work, val) (false)
#define gen_gbt_work(pool, work) {}

int dev_from_id(int thr_id)
{
	struct cgpu_info *cgpu = get_thr_cgpu(thr_id);

	return cgpu->device_id;
}

/* Create an exponentially decaying average over the opt_log_interval */
void decay_time(double *f, double fadd, double fsecs, double interval)
{
	double ftotal, fprop;

	if (fsecs <= 0)
		return;
	fprop = 1.0 - 1 / (exp(fsecs / interval));
	ftotal = 1.0 + fprop;
	*f += (fadd / fsecs * fprop);
	*f /= ftotal;
}

static int __total_staged(void)
{
	return HASH_COUNT(staged_work);
}


double total_secs = 1.0;
double last_total_secs = 1.0;
static char statusline[256];
/* logstart is where the log window should start */
static int devcursor, logstart, logcursor;


/* Convert a uint64_t value into a truncated string for displaying with its
 * associated suitable for Mega, Giga etc. Buf array needs to be long enough */
static void suffix_string(uint64_t val, char *buf, size_t bufsiz, int sigdigits)
{
	const double  dkilo = 1000.0;
	const uint64_t kilo = 1000ull;
	const uint64_t mega = 1000000ull;
	const uint64_t giga = 1000000000ull;
	const uint64_t tera = 1000000000000ull;
	const uint64_t peta = 1000000000000000ull;
	const uint64_t exa  = 1000000000000000000ull;
	char suffix[2] = "";
	bool decimal = true;
	double dval;

	if (val >= exa) {
		val /= peta;
		dval = (double)val / dkilo;
		strcpy(suffix, "E");
	} else if (val >= peta) {
		val /= tera;
		dval = (double)val / dkilo;
		strcpy(suffix, "P");
	} else if (val >= tera) {
		val /= giga;
		dval = (double)val / dkilo;
		strcpy(suffix, "T");
	} else if (val >= giga) {
		val /= mega;
		dval = (double)val / dkilo;
		strcpy(suffix, "G");
	} else if (val >= mega) {
		val /= kilo;
		dval = (double)val / dkilo;
		strcpy(suffix, "M");
	} else if (val >= kilo) {
		dval = (double)val / dkilo;
		strcpy(suffix, "K");
	} else {
		dval = val;
		decimal = false;
	}

	if (!sigdigits) {
		if (decimal)
			snprintf(buf, bufsiz, "%.3g%s", dval, suffix);
		else
			snprintf(buf, bufsiz, "%d%s", (unsigned int)dval, suffix);
	} else {
		/* Always show sigdigits + 1, padded on right with zeroes
		 * followed by suffix */
		int ndigits = sigdigits - 1 - (dval > 0.0 ? floor(log10(dval)) : 0);

		snprintf(buf, bufsiz, "%*.*f%s", sigdigits + 1, ndigits, dval, suffix);
	}
}

double cgpu_runtime(struct cgpu_info *cgpu)
{
	struct timeval now;
	double dev_runtime;

	if (cgpu->dev_start_tv.tv_sec == 0)
		dev_runtime = total_secs;
	else {
		cgtime(&now);
		dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
	}

	if (dev_runtime < 1.0)
		dev_runtime = 1.0;
	return dev_runtime;
}

double tsince_restart(void)
{
	struct timeval now;

	cgtime(&now);
	return tdiff(&now, &restart_tv_start);
}

double tsince_update(void)
{
	struct timeval now;

	cgtime(&now);
	return tdiff(&now, &update_tv_start);
}

static void get_statline(char *buf, size_t bufsiz, struct cgpu_info *cgpu)
{
	char displayed_hashes[16], displayed_rolling[16];
	double dev_runtime, wu;
	uint64_t dh64, dr64;

	dev_runtime = cgpu_runtime(cgpu);

	wu = cgpu->diff1 / dev_runtime * 60.0;

	dh64 = (double)cgpu->total_mhashes / dev_runtime * 1000000ull;
	dr64 = (double)cgpu->rolling * 1000000ull;
	suffix_string(dh64, displayed_hashes, sizeof(displayed_hashes), 4);
	suffix_string(dr64, displayed_rolling, sizeof(displayed_rolling), 4);

	snprintf(buf, bufsiz, "%s%d ", cgpu->drv->name, cgpu->device_id);
	cgpu->drv->get_statline_before(buf, bufsiz, cgpu);
	tailsprintf(buf, bufsiz, "(%ds):%s (avg):%sh/s | A:%.0f R:%.0f HW:%d WU:%.1f/m",
		opt_log_interval,
		displayed_rolling,
		displayed_hashes,
		cgpu->diff_accepted,
		cgpu->diff_rejected,
		cgpu->hw_errors,
		wu);
	cgpu->drv->get_statline(buf, bufsiz, cgpu);
}

static bool shared_strategy(void)
{
	return (pool_strategy == POOL_LOADBALANCE || pool_strategy == POOL_BALANCE);
}



static void enable_pool(struct pool *pool)
{
	if (pool->enabled != POOL_ENABLED) {
		enabled_pools++;
		pool->enabled = POOL_ENABLED;
	}
}


static void reject_pool(struct pool *pool)
{
	if (pool->enabled == POOL_ENABLED)
		enabled_pools--;
	pool->enabled = POOL_REJECTING;
}

static void restart_threads(void);

/* Theoretically threads could race when modifying accepted and
 * rejected values but the chance of two submits completing at the
 * same time is zero so there is no point adding extra locking */
static void
share_result(json_t *val, json_t *res, json_t *err, const struct work *work,
	     char *hashshow, bool resubmit, char *worktime)
{
	struct pool *pool = work->pool;
	struct cgpu_info *cgpu;

	cgpu = get_thr_cgpu(work->thr_id);

	if (json_is_true(res) || (work->gbt && json_is_null(res))) {
		mutex_lock(&stats_lock);
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		mutex_unlock(&stats_lock);

		pool->seq_rejects = 0;
		cgpu->last_share_pool = pool->pool_no;
		cgpu->last_share_pool_time = time(NULL);
		cgpu->last_share_diff = work->work_difficulty;
		pool->last_share_time = cgpu->last_share_pool_time;
		pool->last_share_diff = work->work_difficulty;
		applog(LOG_DEBUG, "PROOF OF WORK RESULT: true (yay!!!)");
		if (!QUIET) {
			if (total_pools > 1)
				applog(LOG_NOTICE, "Accepted %s %s %d pool %d %s%s",
				       hashshow, cgpu->drv->name, cgpu->device_id, work->pool->pool_no, resubmit ? "(resubmit)" : "", worktime);
			else
				applog(LOG_NOTICE, "Accepted %s %s %d %s%s",
				       hashshow, cgpu->drv->name, cgpu->device_id, resubmit ? "(resubmit)" : "", worktime);
		}
		sharelog("accept", work);
		if (opt_shares && total_diff_accepted >= opt_shares) {
			applog(LOG_WARNING, "Successfully mined %d accepted shares as requested and exiting.", opt_shares);
			kill_work();
			return;
		}

		/* Detect if a pool that has been temporarily disabled for
		 * continually rejecting shares has started accepting shares.
		 * This will only happen with the work returned from a
		 * longpoll */
		if (unlikely(pool->enabled == POOL_REJECTING)) {
			applog(LOG_WARNING, "Rejecting pool %d now accepting shares, re-enabling!", pool->pool_no);
			enable_pool(pool);
			switch_pools(NULL);
		}
		/* If we know we found the block we know better than anyone
		 * that new work is needed. */
		if (unlikely(work->block))
			restart_threads();
	} else {
		mutex_lock(&stats_lock);
		cgpu->rejected++;
		total_rejected++;
		pool->rejected++;
		cgpu->diff_rejected += work->work_difficulty;
		total_diff_rejected += work->work_difficulty;
		pool->diff_rejected += work->work_difficulty;
		pool->seq_rejects++;
		mutex_unlock(&stats_lock);

		applog(LOG_DEBUG, "PROOF OF WORK RESULT: false (booooo)");
		if (!QUIET) {
			char where[20];
			char disposition[36] = "reject";
			char reason[32];

			strcpy(reason, "");
			if (total_pools > 1)
				snprintf(where, sizeof(where), "pool %d", work->pool->pool_no);
			else
				strcpy(where, "");

			if (!work->gbt)
				res = json_object_get(val, "reject-reason");
			if (res) {
				const char *reasontmp = json_string_value(res);

				size_t reasonLen = strlen(reasontmp);
				if (reasonLen > 28)
					reasonLen = 28;
				reason[0] = ' '; reason[1] = '(';
				memcpy(2 + reason, reasontmp, reasonLen);
				reason[reasonLen + 2] = ')'; reason[reasonLen + 3] = '\0';
				memcpy(disposition + 7, reasontmp, reasonLen);
				disposition[6] = ':'; disposition[reasonLen + 7] = '\0';
			} else if (work->stratum && err) {
				if (json_is_array(err)) {
					json_t *reason_val = json_array_get(err, 1);
					char *reason_str;

					if (reason_val && json_is_string(reason_val)) {
						reason_str = (char *)json_string_value(reason_val);
						snprintf(reason, 31, " (%s)", reason_str);
					}
				} else if (json_is_string(err)) {
					const char *s = json_string_value(err);
					snprintf(reason, 31, " (%s)", s);
				}
			}

			applog(LOG_NOTICE, "Rejected %s %s %d %s%s %s%s",
			       hashshow, cgpu->drv->name, cgpu->device_id, where, reason, resubmit ? "(resubmit)" : "", worktime);
			sharelog(disposition, work);
		}

		/* Once we have more than a nominal amount of sequential rejects,
		 * at least 10 and more than 3 mins at the current utility,
		 * disable the pool because some pool error is likely to have
		 * ensued. Do not do this if we know the share just happened to
		 * be stale due to networking delays.
		 */
		if (pool->seq_rejects > 10 && !work->stale && opt_disable_pool && enabled_pools > 1) {
			double utility = total_accepted / total_secs * 60;

			if (pool->seq_rejects > utility * 3 && enabled_pools > 1) {
				applog(LOG_WARNING, "Pool %d rejected %d sequential shares, disabling!",
				       pool->pool_no, pool->seq_rejects);
				reject_pool(pool);
				if (pool == current_pool())
					switch_pools(NULL);
				pool->seq_rejects = 0;
			}
		}
	}
}

static void show_hash(struct work *work, char *hashshow)
{
	unsigned char rhash[32];
	char diffdisp[16];
	unsigned long h32;
	uint32_t *hash32;
	uint64_t uintdiff;
	int ofs;

	swab256(rhash, work->hash);
	for (ofs = 0; ofs <= 28; ofs ++) {
		if (rhash[ofs])
			break;
	}
	hash32 = (uint32_t *)(rhash + ofs);
	h32 = be32toh(*hash32);
	uintdiff = round(work->work_difficulty);
	suffix_string(work->share_diff, diffdisp, sizeof (diffdisp), 0);
	snprintf(hashshow, 64, "%08lx Diff %s/%"PRIu64"%s", h32, diffdisp, uintdiff,
		 work->block? " BLOCK!" : "");
}



/* Specifies whether we can use this pool for work or not. */
static bool pool_unworkable(struct pool *pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	if (pool->has_stratum && !pool->stratum_active)
		return true;
	return false;
}

/* In balanced mode, the amount of diff1 solutions per pool is monitored as a
 * rolling average per 10 minutes and if pools start getting more, it biases
 * away from them to distribute work evenly. The share count is reset to the
 * rolling average every 10 minutes to not send all work to one pool after it
 * has been disabled/out for an extended period. */
static struct pool *select_balanced(struct pool *cp)
{
	int i, lowest = cp->shares;
	struct pool *ret = cp;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool_unworkable(pool))
			continue;
		if (pool->shares < lowest) {
			lowest = pool->shares;
			ret = pool;
		}
	}

	ret->shares++;
	return ret;
}

static struct pool *priority_pool(int choice);
static bool pool_unusable(struct pool *pool);

/* Select any active pool in a rotating fashion when loadbalance is chosen if
 * it has any quota left. */
static inline struct pool *select_pool(bool lagging)
{
	static int rotating_pool = 0;
	struct pool *pool, *cp;
	bool avail = false;
	int tested, i;

	cp = current_pool();

	if (pool_strategy == POOL_BALANCE) {
		pool = select_balanced(cp);
		goto out;
	}

	if (pool_strategy != POOL_LOADBALANCE && (!lagging || opt_fail_only)) {
		pool = cp;
		goto out;
	} else
		pool = NULL;

	for (i = 0; i < total_pools; i++) {
		struct pool *tp = pools[i];

		if (tp->quota_used < tp->quota_gcd) {
			avail = true;
			break;
		}
	}

	/* There are no pools with quota, so reset them. */
	if (!avail) {
		for (i = 0; i < total_pools; i++)
			pools[i]->quota_used = 0;
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
	}

	/* Try to find the first pool in the rotation that is usable */
	tested = 0;
	while (!pool && tested++ < total_pools) {
		pool = pools[rotating_pool];
		if (pool->quota_used++ < pool->quota_gcd) {
			if (!pool_unworkable(pool))
				break;
			/* Failover-only flag for load-balance means distribute
			 * unused quota to priority pool 0. */
			if (opt_fail_only)
				priority_pool(0)->quota_used--;
		}
		pool = NULL;
		if (++rotating_pool >= total_pools)
			rotating_pool = 0;
	}

	/* If there are no alive pools with quota, choose according to
	 * priority. */
	if (!pool) {
		for (i = 0; i < total_pools; i++) {
			struct pool *tp = priority_pool(i);

			if (!pool_unusable(tp)) {
				pool = tp;
				break;
			}
		}
	}

	/* If still nothing is usable, use the current pool */
	if (!pool)
		pool = cp;
out:
	applog(LOG_DEBUG, "Selecting pool %d for work", pool->pool_no);
	return pool;
}

/* truediffone == 0x00000000FFFF0000000000000000000000000000000000000000000000000000
 * Generate a 256 bit binary LE target by cutting up diff into 64 bit sized
 * portions or vice versa. */
static const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;
static const double bits192 = 6277101735386680763835789423207666416102355444464034512896.0;
static const double bits128 = 340282366920938463463374607431768211456.0;
static const double bits64 = 18446744073709551616.0;

/* Converts a little endian 256 bit value to a double */
static double le256todouble(const void *target)
{
	uint64_t *data64;
	double dcut64;

	data64 = (uint64_t *)(target + 24);
	dcut64 = le64toh(*data64) * bits192;

	data64 = (uint64_t *)(target + 16);
	dcut64 += le64toh(*data64) * bits128;

	data64 = (uint64_t *)(target + 8);
	dcut64 += le64toh(*data64) * bits64;

	data64 = (uint64_t *)(target);
	dcut64 += le64toh(*data64);

	return dcut64;
}

static double diff_from_target(void *target)
{
	double d64, dcut64;

	d64 = truediffone;
	dcut64 = le256todouble(target);
	if (unlikely(!dcut64))
		dcut64 = 1;
	return d64 / dcut64;
}

/*
 * Calculate the work->work_difficulty based on the work->target
 */
static void calc_diff(struct work *work, double known)
{
	struct cgminer_pool_stats *pool_stats = &(work->pool->cgminer_pool_stats);
	double difficulty;
	uint64_t uintdiff;

	if (known)
		work->work_difficulty = known;
	else
		work->work_difficulty = diff_from_target(work->target);

	difficulty = work->work_difficulty;

	pool_stats->last_diff = difficulty;
	uintdiff = round(difficulty);
	suffix_string(uintdiff, work->pool->diff, sizeof(work->pool->diff), 0);

	if (difficulty == pool_stats->min_diff)
		pool_stats->min_diff_count++;
	else if (difficulty < pool_stats->min_diff || pool_stats->min_diff == 0) {
		pool_stats->min_diff = difficulty;
		pool_stats->min_diff_count = 1;
	}

	if (difficulty == pool_stats->max_diff)
		pool_stats->max_diff_count++;
	else if (difficulty > pool_stats->max_diff) {
		pool_stats->max_diff = difficulty;
		pool_stats->max_diff_count = 1;
	}
}

static unsigned char bench_hidiff_bins[16][160];
static unsigned char bench_lodiff_bins[16][160];
static unsigned char bench_target[32];

/* Iterate over the lo and hi diff benchmark work items such that we find one
 * diff 32+ share every 32 work items. */
static void get_benchmark_work(struct work *work)
{
	work->work_difficulty = 32;
	memcpy(work->target, bench_target, 32);
	work->drv_rolllimit = 0;
	work->mandatory = true;
	work->pool = pools[0];
	cgtime(&work->tv_getwork);
	copy_time(&work->tv_getwork_reply, &work->tv_getwork);
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
}

static void benchfile_dspwork(struct work *work, uint32_t nonce)
{
	char buf[1024];
	uint32_t dn;
	int i;

	dn = 0;
	for (i = 0; i < 4; i++) {
		dn *= 0x100;
		dn += nonce & 0xff;
		nonce /= 0x100;
	}

	if ((sizeof(work->data) * 2 + 1) > sizeof(buf))
		quithere(1, "BENCHFILE Invalid buf size");

	__bin2hex(buf, work->data, sizeof(work->data));

	applog(LOG_ERR, "BENCHFILE nonce %u=0x%08x for work=%s",
			(unsigned int)dn, (unsigned int)dn, buf);

}

static bool benchfile_get_work(struct work *work)
{
	char buf[1024];
	char item[1024];
	bool got = false;

	if (!benchfile_in) {
		if (opt_benchfile)
			benchfile_in = fopen(opt_benchfile, "r");
		else
			quit(1, "BENCHFILE Invalid benchfile NULL");

		if (!benchfile_in)
			quit(1, "BENCHFILE Failed to open benchfile '%s'", opt_benchfile);

		benchfile_line = 0;

		if (!fgets(buf, 1024, benchfile_in))
			quit(1, "BENCHFILE Failed to read benchfile '%s'", opt_benchfile);

		got = true;
		benchfile_work = 0;
	}

	if (!got) {
		if (!fgets(buf, 1024, benchfile_in)) {
			if (benchfile_work == 0)
				quit(1, "BENCHFILE No work in benchfile '%s'", opt_benchfile);
			fclose(benchfile_in);
			benchfile_in = NULL;
			return benchfile_get_work(work);
		}
	}

	do {
		benchfile_line++;

		// Empty lines and lines starting with '#' or '/' are ignored
		if (*buf != '\0' && *buf != '#' && *buf != '/') {
			char *commas[BENCHWORK_COUNT];
			int i, j, len;
			long nonce_time;

			commas[0] = buf;
			for (i = 1; i < BENCHWORK_COUNT; i++) {
				commas[i] = strchr(commas[i-1], ',');
				if (!commas[i]) {
					quit(1, "BENCHFILE Invalid input file line %d"
						" - field count is %d but should be %d",
						benchfile_line, i, BENCHWORK_COUNT);
				}
				len = commas[i] - commas[i-1];
				if (benchfile_data[i-1].length &&
				    (len != benchfile_data[i-1].length)) {
					quit(1, "BENCHFILE Invalid input file line %d "
						"field %d (%s) length is %d but should be %d",
						benchfile_line, i,
						benchfile_data[i-1].name,
						len, benchfile_data[i-1].length);
				}

				*(commas[i]++) = '\0';
			}

			// NonceTime may have LF's etc
			len = strlen(commas[BENCHWORK_NONCETIME]);
			if (len < benchfile_data[BENCHWORK_NONCETIME].length) {
				quit(1, "BENCHFILE Invalid input file line %d field %d"
					" (%s) length is %d but should be least %d",
					benchfile_line, BENCHWORK_NONCETIME+1,
					benchfile_data[BENCHWORK_NONCETIME].name, len,
					benchfile_data[BENCHWORK_NONCETIME].length);
			}

			sprintf(item, "0000000%c", commas[BENCHWORK_VERSION][0]);

			j = strlen(item);
			for (i = benchfile_data[BENCHWORK_PREVHASH].length-8; i >= 0; i -= 8) {
				sprintf(&(item[j]), "%.8s", &commas[BENCHWORK_PREVHASH][i]);
				j += 8;
			}

			for (i = benchfile_data[BENCHWORK_MERKLEROOT].length-8; i >= 0; i -= 8) {
				sprintf(&(item[j]), "%.8s", &commas[BENCHWORK_MERKLEROOT][i]);
				j += 8;
			}

			nonce_time = atol(commas[BENCHWORK_NONCETIME]);

			sprintf(&(item[j]), "%08lx", nonce_time);
			j += 8;

			strcpy(&(item[j]), commas[BENCHWORK_DIFFBITS]);
			j += benchfile_data[BENCHWORK_DIFFBITS].length;

			memset(work, 0, sizeof(*work));

			hex2bin(work->data, item, j >> 1);

			calc_midstate(work);

			benchfile_work++;

			return true;
		}
	} while (fgets(buf, 1024, benchfile_in));

	if (benchfile_work == 0)
		quit(1, "BENCHFILE No work in benchfile '%s'", opt_benchfile);
	fclose(benchfile_in);
	benchfile_in = NULL;
	return benchfile_get_work(work);
}

static void get_benchfile_work(struct work *work)
{
	benchfile_get_work(work);
	work->mandatory = true;
	work->pool = pools[0];
	cgtime(&work->tv_getwork);
	copy_time(&work->tv_getwork_reply, &work->tv_getwork);
	work->getwork_mode = GETWORK_MODE_BENCHMARK;
	calc_diff(work, 0);
}

static void kill_timeout(struct thr_info *thr)
{
	cg_completion_timeout(&thr_info_cancel, thr, 1000);
}

static void kill_mining(void)
{
	struct thr_info *thr;
	int i;

	forcelog(LOG_DEBUG, "Killing off mining threads");
	/* Kill the mining threads*/
	for (i = 0; i < mining_threads; i++) {
		pthread_t *pth = NULL;

		thr = get_thread(i);
		if (thr && PTH(thr) != 0L)
			pth = &thr->pth;
		thr_info_cancel(thr);
#ifndef WIN32
		if (pth && *pth)
			pthread_join(*pth, NULL);
#else
		if (pth && pth->p)
			pthread_join(*pth, NULL);
#endif
	}
}

static void __kill_work(void)
{
	struct thr_info *thr;
	int i;

	if (!successful_connect)
		return;

	forcelog(LOG_INFO, "Received kill message");

#ifdef USE_USBUTILS
	/* Best to get rid of it first so it doesn't
	 * try to create any new devices */
	forcelog(LOG_DEBUG, "Killing off HotPlug thread");
	thr = &control_thr[hotplug_thr_id];
	kill_timeout(thr);
#endif

	forcelog(LOG_DEBUG, "Killing off watchpool thread");
	/* Kill the watchpool thread */
	thr = &control_thr[watchpool_thr_id];
	kill_timeout(thr);

	forcelog(LOG_DEBUG, "Killing off watchdog thread");
	/* Kill the watchdog thread */
	thr = &control_thr[watchdog_thr_id];
	kill_timeout(thr);

	forcelog(LOG_DEBUG, "Shutting down mining threads");
	for (i = 0; i < mining_threads; i++) {
		struct cgpu_info *cgpu;

		thr = get_thread(i);
		if (!thr)
			continue;
		cgpu = thr->cgpu;
		if (!cgpu)
			continue;

		cgpu->shutdown = true;
	}

	sleep(1);

	cg_completion_timeout(&kill_mining, NULL, 3000);

	/* Stop the others */
	forcelog(LOG_DEBUG, "Killing off API thread");
	thr = &control_thr[api_thr_id];
	kill_timeout(thr);

#ifdef USE_USBUTILS
	/* Release USB resources in case it's a restart
	 * and not a QUIT */
	forcelog(LOG_DEBUG, "Releasing all USB devices");
	cg_completion_timeout(&usb_cleanup, NULL, 1000);

	forcelog(LOG_DEBUG, "Killing off usbres thread");
	thr = &control_thr[usbres_thr_id];
	kill_timeout(thr);
#endif

}

/* This should be the common exit path */
void kill_work(void)
{
	cg_completion_timeout(&__kill_work, NULL, 5000);

	quit(0, "Shutdown signal received.");
}

static
#ifdef WIN32
const
#endif
char **initial_args;

static void clean_up(bool restarting);

void app_restart(void)
{
	applog(LOG_WARNING, "Attempting to restart %s", packagename);

	cg_completion_timeout(&__kill_work, NULL, 5000);
	clean_up(true);

#if defined(unix) || defined(__APPLE__)
	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
#endif

	execv(initial_args[0], (EXECV_2ND_ARG_TYPE)initial_args);
	applog(LOG_WARNING, "Failed to restart application");
}

static void sighandler(int __maybe_unused sig)
{
	/* Restore signal handlers so we can still quit if kill_work fails */
	sigaction(SIGTERM, &termhandler, NULL);
	sigaction(SIGINT, &inthandler, NULL);
	kill_work();
}

static void _stage_work(struct work *work);

#define stage_work(WORK) do { \
	_stage_work(WORK); \
	WORK = NULL; \
} while (0)

/* Adjust an existing char ntime field with a relative noffset */
static void modify_ntime(char *ntime, int noffset)
{
	unsigned char bin[4];
	uint32_t h32, *be32 = (uint32_t *)bin;

	hex2bin(bin, ntime, 4);
	h32 = be32toh(*be32) + noffset;
	*be32 = htobe32(h32);
	__bin2hex(ntime, bin, 4);
}

void roll_work(struct work *work)
{
	uint32_t *work_ntime;
	uint32_t ntime;

	work_ntime = (uint32_t *)(work->data + 68);
	ntime = be32toh(*work_ntime);
	ntime++;
	*work_ntime = htobe32(ntime);
	local_work++;
	work->rolls++;
	work->nonce = 0;
	applog(LOG_DEBUG, "Successfully rolled work");
	/* Change the ntime field if this is stratum work */
	if (work->ntime)
		modify_ntime(work->ntime, 1);

	/* This is now a different work item so it needs a different ID for the
	 * hashtable */
	work->id = total_work_inc();
}

struct work *make_clone(struct work *work)
{
	struct work *work_clone = copy_work(work);

	work_clone->clone = true;
	cgtime((struct timeval *)&(work_clone->tv_cloned));
	work_clone->longpoll = false;
	work_clone->mandatory = false;
	/* Make cloned work appear slightly older to bias towards keeping the
	 * master work item which can be further rolled */
	work_clone->tv_staged.tv_sec -= 1;

	return work_clone;
}

static void *submit_work_thread(void __maybe_unused *userdata)
{
	pthread_detach(pthread_self());
	return NULL;
}


/* Return an adjusted ntime if we're submitting work that a device has
 * internally offset the ntime. */
static char *offset_ntime(const char *ntime, int noffset)
{
	unsigned char bin[4];
	uint32_t h32, *be32 = (uint32_t *)bin;

	hex2bin(bin, ntime, 4);
	h32 = be32toh(*be32) + noffset;
	*be32 = htobe32(h32);

	return bin2hex(bin, 4);
}

/* Duplicates any dynamically allocated arrays within the work struct to
 * prevent a copied work struct from freeing ram belonging to another struct */
static void _copy_work(struct work *work, const struct work *base_work, int noffset)
{
	uint32_t id = work->id;

	clean_work(work);
	memcpy(work, base_work, sizeof(struct work));
	/* Keep the unique new id assigned during make_work to prevent copied
	 * work from having the same id. */
	work->id = id;
	if (base_work->job_id)
		work->job_id = strdup(base_work->job_id);
	if (base_work->nonce1)
		work->nonce1 = strdup(base_work->nonce1);
	if (base_work->ntime) {
		/* If we are passed an noffset the binary work->data ntime and
		 * the work->ntime hex string need to be adjusted. */
		if (noffset) {
			uint32_t *work_ntime = (uint32_t *)(work->data + 68);
			uint32_t ntime = be32toh(*work_ntime);

			ntime += noffset;
			*work_ntime = htobe32(ntime);
			work->ntime = offset_ntime(base_work->ntime, noffset);
		} else
			work->ntime = strdup(base_work->ntime);
	} else if (noffset) {
		uint32_t *work_ntime = (uint32_t *)(work->data + 68);
		uint32_t ntime = be32toh(*work_ntime);

		ntime += noffset;
		*work_ntime = htobe32(ntime);
	}
	if (base_work->coinbase)
		work->coinbase = strdup(base_work->coinbase);
}

void set_work_ntime(struct work *work, int ntime)
{
	uint32_t *work_ntime = (uint32_t *)(work->data + 68);

	*work_ntime = htobe32(ntime);
	if (work->ntime) {
		free(work->ntime);
		work->ntime = bin2hex((unsigned char *)work_ntime, 4);
	}
}

/* Generates a copy of an existing work struct, creating fresh heap allocations
 * for all dynamically allocated arrays within the struct. noffset is used for
 * when a driver has internally rolled the ntime, noffset is a relative value.
 * The macro copy_work() calls this function with an noffset of 0. */
struct work *copy_work_noffset(struct work *base_work, int noffset)
{
	struct work *work = make_work();

	_copy_work(work, base_work, noffset);

	return work;
}

void pool_died(struct pool *pool)
{
	if (!pool_tset(pool, &pool->idle)) {
		cgtime(&pool->tv_idle);
		if (pool == current_pool()) {
			applog(LOG_WARNING, "Pool %d %s not responding!", pool->pool_no, pool->rpc_url);
			switch_pools(NULL);
		} else
			applog(LOG_INFO, "Pool %d %s failed to return work", pool->pool_no, pool->rpc_url);
	}
}

static bool stale_work(struct work *work, bool share)
{
	struct timeval now;
	time_t work_expiry;
	struct pool *pool;
	int getwork_delay;

	if (opt_benchmark || opt_benchfile)
		return false;

	if (work->work_block != work_block) {
		applog(LOG_DEBUG, "Work stale due to block mismatch");
		return true;
	}

	/* Technically the rolltime should be correct but some pools
	 * advertise a broken expire= that is lower than a meaningful
	 * scantime */
	if (work->rolltime > opt_scantime)
		work_expiry = work->rolltime;
	else
		work_expiry = opt_expiry;

	pool = work->pool;

	if (!share && pool->has_stratum) {
		bool same_job;

		if (!pool->stratum_active || !pool->stratum_notify) {
			applog(LOG_DEBUG, "Work stale due to stratum inactive");
			return true;
		}

		same_job = true;

		cg_rlock(&pool->data_lock);
		if (strcmp(work->job_id, pool->swork.job_id))
			same_job = false;
		cg_runlock(&pool->data_lock);

		if (!same_job) {
			applog(LOG_DEBUG, "Work stale due to stratum job_id mismatch");
			return true;
		}
	}

	/* Factor in the average getwork delay of this pool, rounding it up to
	 * the nearest second */
	getwork_delay = pool->cgminer_pool_stats.getwork_wait_rolling * 5 + 1;
	work_expiry -= getwork_delay;
	if (unlikely(work_expiry < 5))
		work_expiry = 5;

	cgtime(&now);
	if ((now.tv_sec - work->tv_staged.tv_sec) >= work_expiry) {
		applog(LOG_DEBUG, "Work stale due to expiry");
		return true;
	}

	if (opt_fail_only && !share && pool != current_pool() && !work->mandatory &&
	    pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		applog(LOG_DEBUG, "Work stale due to fail only pool mismatch");
		return true;
	}

	return false;
}

uint64_t share_diff(const struct work *work)
{
	bool new_best = false;
	double d64, s64;
	uint64_t ret;

	d64 = truediffone;
	s64 = le256todouble(work->hash);
	if (unlikely(!s64))
		s64 = 0;

	ret = round(d64 / s64);

	cg_wlock(&control_lock);
	if (unlikely(ret > best_diff)) {
		new_best = true;
		best_diff = ret;
		suffix_string(best_diff, best_share, sizeof(best_share), 0);
	}
	if (unlikely(ret > work->pool->best_diff))
		work->pool->best_diff = ret;
	cg_wunlock(&control_lock);

	if (unlikely(new_best))
		applog(LOG_INFO, "New best share: %s", best_share);

	return ret;
}

uint64_t share_ndiff(const struct work *work)
{
	double d64, s64;
	uint64_t ret = 0;

	if(work != NULL) {
		d64 = truediffone;
		s64 = le256todouble(work->hash);
		if (unlikely(!s64)) {
			ret = 0;
		} else {
			ret = (d64 / s64);
		}
	}
	return ret;
}

static void regen_hash(struct work *work)
{
	uint32_t *data32 = (uint32_t *)(work->data);
	unsigned char swap[80];
	uint32_t *swap32 = (uint32_t *)swap;
	unsigned char hash1[32];

	flip80(swap32, data32);
	sha256(swap, 80, hash1);
	sha256(hash1, 32, (unsigned char *)(work->hash));
}

static bool cnx_needed(struct pool *pool);

/* Find the pool that currently has the highest priority */
static struct pool *priority_pool(int choice)
{
	struct pool *ret = NULL;
	int i;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		if (pool->prio == choice) {
			ret = pool;
			break;
		}
	}

	if (unlikely(!ret)) {
		applog(LOG_ERR, "WTF No pool %d found!", choice);
		return pools[choice];
	}
	return ret;
}

/* Specifies whether we can switch to this pool or not. */
static bool pool_unusable(struct pool *pool)
{
	if (pool->idle)
		return true;
	if (pool->enabled != POOL_ENABLED)
		return true;
	return false;
}

void switch_pools(struct pool *selected)
{
	struct pool *pool, *last_pool;
	int i, pool_no, next_pool;

	cg_wlock(&control_lock);
	last_pool = currentpool;
	pool_no = currentpool->pool_no;

	/* Switch selected to pool number 0 and move the rest down */
	if (selected) {
		if (selected->prio != 0) {
			for (i = 0; i < total_pools; i++) {
				pool = pools[i];
				if (pool->prio < selected->prio)
					pool->prio++;
			}
			selected->prio = 0;
		}
	}

	switch (pool_strategy) {
		/* All of these set to the master pool */
		case POOL_BALANCE:
		case POOL_FAILOVER:
		case POOL_LOADBALANCE:
			for (i = 0; i < total_pools; i++) {
				pool = priority_pool(i);
				if (pool_unusable(pool))
					continue;
				pool_no = pool->pool_no;
				break;
			}
			break;
		/* Both of these simply increment and cycle */
		case POOL_ROUNDROBIN:
		case POOL_ROTATE:
			if (selected && !selected->idle) {
				pool_no = selected->pool_no;
				break;
			}
			next_pool = pool_no;
			/* Select the next alive pool */
			for (i = 1; i < total_pools; i++) {
				next_pool++;
				if (next_pool >= total_pools)
					next_pool = 0;
				pool = pools[next_pool];
				if (pool_unusable(pool))
					continue;
				pool_no = next_pool;
				break;
			}
			break;
		default:
			break;
	}

	currentpool = pools[pool_no];
	pool = currentpool;
	cg_wunlock(&control_lock);

	/* Set the lagging flag to avoid pool not providing work fast enough
	 * messages in failover only mode since  we have to get all fresh work
	 * as in restart_threads */
	if (opt_fail_only)
		pool_tset(pool, &pool->lagging);

	if (pool != last_pool && pool_strategy != POOL_LOADBALANCE && pool_strategy != POOL_BALANCE) {
		applog(LOG_WARNING, "Switching to pool %d %s", pool->pool_no, pool->rpc_url);
		if (pool_localgen(pool) || opt_fail_only)
			clear_pool_work(last_pool);
	}

	mutex_lock(&lp_lock);
	pthread_cond_broadcast(&lp_cond);
	mutex_unlock(&lp_lock);

}

void _discard_work(struct work *work)
{
	if (!work->clone && !work->rolls && !work->mined) {
		if (work->pool) {
			work->pool->discarded_work++;
			work->pool->quota_used--;
			work->pool->works--;
		}
		total_discarded++;
		applog(LOG_DEBUG, "Discarded work");
	} else
		applog(LOG_DEBUG, "Discarded cloned or rolled work");
	free_work(work);
}

static void wake_gws(void)
{
	mutex_lock(stgd_lock);
	pthread_cond_signal(&gws_cond);
	mutex_unlock(stgd_lock);
}

static void discard_stale(void)
{
	struct work *work, *tmp;
	int stale = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (stale_work(work, false)) {
			HASH_DEL(staged_work, work);
			discard_work(work);
			stale++;
		}
	}
	pthread_cond_signal(&gws_cond);
	mutex_unlock(stgd_lock);

	if (stale)
		applog(LOG_DEBUG, "Discarded %d stales that didn't match current hash", stale);
}

/* A generic wait function for threads that poll that will wait a specified
 * time tdiff waiting on the pthread conditional that is broadcast when a
 * work restart is required. Returns the value of pthread_cond_timedwait
 * which is zero if the condition was met or ETIMEDOUT if not.
 */
int restart_wait(struct thr_info *thr, unsigned int mstime)
{
	struct timeval now, then, tdiff;
	struct timespec abstime;
	int rc;

	tdiff.tv_sec = mstime / 1000;
	tdiff.tv_usec = mstime * 1000 - (tdiff.tv_sec * 1000000);
	cgtime(&now);
	timeradd(&now, &tdiff, &then);
	abstime.tv_sec = then.tv_sec;
	abstime.tv_nsec = then.tv_usec * 1000;

	mutex_lock(&restart_lock);
	if (thr->work_restart)
		rc = 0;
	else
		rc = pthread_cond_timedwait(&restart_cond, &restart_lock, &abstime);
	mutex_unlock(&restart_lock);

	return rc;
}

static void *restart_thread(void __maybe_unused *arg)
{
	struct pool *cp = current_pool();
	struct cgpu_info *cgpu;
	int i, mt;

	pthread_detach(pthread_self());

	/* Artificially set the lagging flag to avoid pool not providing work
	 * fast enough  messages after every long poll */
	pool_tset(cp, &cp->lagging);

	/* Discard staged work that is now stale */
	discard_stale();

	rd_lock(&mining_thr_lock);
	mt = mining_threads;
	rd_unlock(&mining_thr_lock);

	for (i = 0; i < mt; i++) {
		cgpu = mining_thr[i]->cgpu;
		if (unlikely(!cgpu))
			continue;
		if (cgpu->deven != DEV_ENABLED)
			continue;
		mining_thr[i]->work_restart = true;
		flush_queue(cgpu);
		cgpu->drv->flush_work(cgpu);
	}

	mutex_lock(&restart_lock);
	pthread_cond_broadcast(&restart_cond);
	mutex_unlock(&restart_lock);

#ifdef USE_USBUTILS
	/* Cancels any cancellable usb transfers. Flagged as such it means they
	 * are usualy waiting on a read result and it's safe to abort the read
	 * early. */
	cancel_usb_transfers();
#endif
	return NULL;
}

/* In order to prevent a deadlock via the various drv->flush_work
 * implementations we send the restart messages via a separate thread. */
static void restart_threads(void)
{
	pthread_t rthread;

	cgtime(&restart_tv_start);
	if (unlikely(pthread_create(&rthread, NULL, restart_thread, NULL)))
		quit(1, "Failed to create restart thread");
}

static void signal_work_update(void)
{
	int i;

	applog(LOG_INFO, "Work update message received");

	cgtime(&update_tv_start);
	rd_lock(&mining_thr_lock);
	for (i = 0; i < mining_threads; i++)
		mining_thr[i]->work_update = true;
	rd_unlock(&mining_thr_lock);
}

static void set_curblock(char *hexstr, unsigned char *bedata)
{
	int ofs;

	cg_wlock(&ch_lock);
	cgtime(&block_timeval);
	strcpy(current_hash, hexstr);
	memcpy(current_block, bedata, 32);
	get_timestamp(blocktime, sizeof(blocktime), &block_timeval);
	cg_wunlock(&ch_lock);

	for (ofs = 0; ofs <= 56; ofs++) {
		if (memcmp(&current_hash[ofs], "0", 1))
			break;
	}
	strncpy(prev_block, &current_hash[ofs], 8);
	prev_block[8] = '\0';

	applog(LOG_INFO, "New block: %s... diff %s", current_hash, block_diff);
}

/* Search to see if this string is from a block that has been seen before */
static bool block_exists(char *hexstr)
{
	struct block *s;

	rd_lock(&blk_lock);
	HASH_FIND_STR(blocks, hexstr, s);
	rd_unlock(&blk_lock);

	if (s)
		return true;
	return false;
}

/* Tests if this work is from a block that has been seen before */
static inline bool from_existing_block(struct work *work)
{
	char *hexstr = bin2hex(work->data + 8, 18);
	bool ret;

	ret = block_exists(hexstr);
	free(hexstr);
	return ret;
}

static int block_sort(struct block *blocka, struct block *blockb)
{
	return blocka->block_no - blockb->block_no;
}

/* Decode the current block difficulty which is in packed form */
static void set_blockdiff(const struct work *work)
{
	uint8_t pow = work->data[72];
	int powdiff = (8 * (0x1d - 3)) - (8 * (pow - 3));
	uint32_t diff32 = be32toh(*((uint32_t *)(work->data + 72))) & 0x00FFFFFF;
	double numerator = 0xFFFFULL << powdiff;
	double ddiff = numerator / (double)diff32;

	if (unlikely(current_diff != ddiff)) {
		suffix_string(ddiff, block_diff, sizeof(block_diff), 0);
		current_diff = ddiff;
		applog(LOG_NOTICE, "Network diff set to %s", block_diff);
	}
}

static bool test_work_current(struct work *work)
{
	struct pool *pool = work->pool;
	unsigned char bedata[32];
	char hexstr[68];
	bool ret = true;

	if (work->mandatory)
		return ret;

	swap256(bedata, work->data + 4);
	__bin2hex(hexstr, bedata, 32);

	/* Search to see if this block exists yet and if not, consider it a
	 * new block and set the current block details to this one */
	if (!block_exists(hexstr)) {
		struct block *s = calloc(sizeof(struct block), 1);
		int deleted_block = 0;

		if (unlikely(!s))
			quit (1, "test_work_current OOM");
		strcpy(s->hash, hexstr);
		s->block_no = new_blocks++;

		wr_lock(&blk_lock);
		/* Only keep the last hour's worth of blocks in memory since
		 * work from blocks before this is virtually impossible and we
		 * want to prevent memory usage from continually rising */
		if (HASH_COUNT(blocks) > 6) {
			struct block *oldblock;

			HASH_SORT(blocks, block_sort);
			oldblock = blocks;
			deleted_block = oldblock->block_no;
			HASH_DEL(blocks, oldblock);
			free(oldblock);
		}
		HASH_ADD_STR(blocks, hash, s);
		set_blockdiff(work);
		wr_unlock(&blk_lock);

		if (deleted_block)
			applog(LOG_DEBUG, "Deleted block %d from database", deleted_block);
		set_curblock(hexstr, bedata);
		/* Copy the information to this pool's prev_block since it
		 * knows the new block exists. */
		memcpy(pool->prev_block, bedata, 32);
		if (unlikely(new_blocks == 1)) {
			ret = false;
			goto out;
		}

		work->work_block = ++work_block;

		if (work->longpoll) {
			if (work->stratum) {
				applog(LOG_NOTICE, "Stratum from pool %d detected new block",
				       pool->pool_no);
			} else {
				applog(LOG_NOTICE, "%sLONGPOLL from pool %d detected new block",
				       work->gbt ? "GBT " : "", work->pool->pool_no);
			}
		} else if (have_longpoll && !pool->gbt_solo)
			applog(LOG_NOTICE, "New block detected on network before pool notification");
		else if (!pool->gbt_solo)
			applog(LOG_NOTICE, "New block detected on network");
		restart_threads();
	} else {
		if (memcmp(pool->prev_block, bedata, 32)) {
			/* Work doesn't match what this pool has stored as
			 * prev_block. Let's see if the work is from an old
			 * block or the pool is just learning about a new
			 * block. */
			if (memcmp(bedata, current_block, 32)) {
				/* Doesn't match current block. It's stale */
				applog(LOG_DEBUG, "Stale data from pool %d", pool->pool_no);
				ret = false;
			} else {
				/* Work is from new block and pool is up now
				 * current. */
				applog(LOG_INFO, "Pool %d now up to date", pool->pool_no);
				memcpy(pool->prev_block, bedata, 32);
			}
		}
#if 0
		/* This isn't ideal, this pool is still on an old block but
		 * accepting shares from it. To maintain fair work distribution
		 * we work on it anyway. */
		if (memcmp(bedata, current_block, 32))
			applog(LOG_DEBUG, "Pool %d still on old block", pool->pool_no);
#endif
		if (work->longpoll) {
			work->work_block = ++work_block;
			if (shared_strategy() || work->pool == current_pool()) {
				if (work->stratum) {
					applog(LOG_NOTICE, "Stratum from pool %d requested work restart",
					       pool->pool_no);
				} else {
					applog(LOG_NOTICE, "%sLONGPOLL from pool %d requested work restart",
					       work->gbt ? "GBT " : "", work->pool->pool_no);
				}
				restart_threads();
			}
		}
	}
out:
	work->longpoll = false;

	return ret;
}

static int tv_sort(struct work *worka, struct work *workb)
{
	return worka->tv_staged.tv_sec - workb->tv_staged.tv_sec;
}

static bool work_rollable(struct work *work)
{
	return (!work->clone && work->rolltime);
}

static bool hash_push(struct work *work)
{
	bool rc = true;

	mutex_lock(stgd_lock);
	if (work_rollable(work))
		staged_rollable++;
	if (likely(!getq->frozen)) {
		HASH_ADD_INT(staged_work, id, work);
		HASH_SORT(staged_work, tv_sort);
	} else
		rc = false;
	pthread_cond_broadcast(&getq->cond);
	mutex_unlock(stgd_lock);

	return rc;
}

static void _stage_work(struct work *work)
{
	applog(LOG_DEBUG, "Pushing work from pool %d to hash queue", work->pool->pool_no);
	work->work_block = work_block;
	test_work_current(work);
	work->pool->works++;
	hash_push(work);
}


/* We can't remove the memory used for this struct pool because there may
 * still be work referencing it. We just remove it from the pools list */
void remove_pool(struct pool *pool)
{
	int i, last_pool = total_pools - 1;
	struct pool *other;

	/* Boost priority of any lower prio than this one */
	for (i = 0; i < total_pools; i++) {
		other = pools[i];
		if (other->prio > pool->prio)
			other->prio--;
	}

	if (pool->pool_no < last_pool) {
		/* Swap the last pool for this one */
		(pools[last_pool])->pool_no = pool->pool_no;
		pools[pool->pool_no] = pools[last_pool];
	}
	/* Give it an invalid number */
	pool->pool_no = total_pools;
	pool->removed = true;
	total_pools--;
}

/* add a mutex if this needs to be thread safe in the future */
static struct JE {
	char *buf;
	struct JE *next;
} *jedata = NULL;

static void json_escape_free()
{
	struct JE *jeptr = jedata;
	struct JE *jenext;

	jedata = NULL;

	while (jeptr) {
		jenext = jeptr->next;
		free(jeptr->buf);
		free(jeptr);
		jeptr = jenext;
	}
}

static char *json_escape(char *str)
{
	struct JE *jeptr;
	char *buf, *ptr;

	/* 2x is the max, may as well just allocate that */
	ptr = buf = malloc(strlen(str) * 2 + 1);

	jeptr = malloc(sizeof(*jeptr));

	jeptr->buf = buf;
	jeptr->next = jedata;
	jedata = jeptr;

	while (*str) {
		if (*str == '\\' || *str == '"')
			*(ptr++) = '\\';

		*(ptr++) = *(str++);
	}

	*ptr = '\0';

	return buf;
}

void write_config(FILE *fcfg)
{
	struct opt_table *opt;
	int i;

	/* Write pool values */
	fputs("{\n\"pools\" : [", fcfg);
	for(i = 0; i < total_pools; i++) {
		struct pool *pool = priority_pool(i);

		if (pool->quota != 1) {
			fprintf(fcfg, "%s\n\t{\n\t\t\"quota\" : \"%s%s%s%d;%s\",", i > 0 ? "," : "",
				pool->rpc_proxy ? json_escape((char *)proxytype(pool->rpc_proxytype)) : "",
				pool->rpc_proxy ? json_escape(pool->rpc_proxy) : "",
				pool->rpc_proxy ? "|" : "",
				pool->quota,
				json_escape(pool->rpc_url));
		} else {
			fprintf(fcfg, "%s\n\t{\n\t\t\"url\" : \"%s%s%s%s\",", i > 0 ? "," : "",
				pool->rpc_proxy ? json_escape((char *)proxytype(pool->rpc_proxytype)) : "",
				pool->rpc_proxy ? json_escape(pool->rpc_proxy) : "",
				pool->rpc_proxy ? "|" : "",
				json_escape(pool->rpc_url));
		}
		fprintf(fcfg, "\n\t\t\"user\" : \"%s\",", json_escape(pool->rpc_user));
		fprintf(fcfg, "\n\t\t\"pass\" : \"%s\"\n\t}", json_escape(pool->rpc_pass));
		}
	fputs("\n]\n", fcfg);

	/* Simple bool,int and char options */
	for (opt = opt_config_table; opt->type != OPT_END; opt++) {
		char *p, *name = strdup(opt->names);

		for (p = strtok(name, "|"); p; p = strtok(NULL, "|")) {
			if (p[1] != '-')
				continue;

			if (opt->desc == opt_hidden)
				continue;

			if (opt->type & OPT_NOARG &&
			   ((void *)opt->cb == (void *)opt_set_bool || (void *)opt->cb == (void *)opt_set_invbool) &&
			   (*(bool *)opt->u.arg == ((void *)opt->cb == (void *)opt_set_bool))) {
				fprintf(fcfg, ",\n\"%s\" : true", p+2);
				continue;
			}

			if (opt->type & OPT_HASARG &&
			    ((void *)opt->cb_arg == (void *)opt_set_intval ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_9999 ||
			     (void *)opt->cb_arg == (void *)set_int_1_to_65535 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_10 ||
			     (void *)opt->cb_arg == (void *)set_int_1_to_10 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_100 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_255 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_200 ||
			     (void *)opt->cb_arg == (void *)set_int_0_to_4 ||
			     (void *)opt->cb_arg == (void *)set_int_32_to_63)) {
				fprintf(fcfg, ",\n\"%s\" : \"%d\"", p+2, *(int *)opt->u.arg);
				continue;
			}

			if (opt->type & OPT_HASARG &&
			    (((void *)opt->cb_arg == (void *)set_float_125_to_500) ||
			     (void *)opt->cb_arg == (void *)set_float_100_to_250)) {
				fprintf(fcfg, ",\n\"%s\" : \"%.1f\"", p+2, *(float *)opt->u.arg);
				continue;
			}

			if (opt->type & (OPT_HASARG | OPT_PROCESSARG) &&
			    (opt->u.arg != &opt_set_null)) {
				char *carg = *(char **)opt->u.arg;

				if (carg)
					fprintf(fcfg, ",\n\"%s\" : \"%s\"", p+2, json_escape(carg));
				continue;
			}
		}
		free(name);
	}

	/* Special case options */
	if (pool_strategy == POOL_BALANCE)
		fputs(",\n\"balance\" : true", fcfg);
	if (pool_strategy == POOL_LOADBALANCE)
		fputs(",\n\"load-balance\" : true", fcfg);
	if (pool_strategy == POOL_ROUNDROBIN)
		fputs(",\n\"round-robin\" : true", fcfg);
	if (pool_strategy == POOL_ROTATE)
		fprintf(fcfg, ",\n\"rotate\" : \"%d\"", opt_rotate_period);
	fputs("\n}\n", fcfg);

	json_escape_free();
}

void zero_bestshare(void)
{
	int i;

	best_diff = 0;
	memset(best_share, 0, 8);
	suffix_string(best_diff, best_share, sizeof(best_share), 0);

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];
		pool->best_diff = 0;
	}
}

static struct timeval tv_hashmeter;
static time_t hashdisplay_t;

void zero_stats(void)
{
	int i;

	cgtime(&total_tv_start);
	copy_time(&tv_hashmeter, &total_tv_start);
	total_rolling = 0;
	rolling1 = 0;
	rolling5 = 0;
	rolling15 = 0;
	total_mhashes_done = 0;

	for(i = 0; i < CG_LOCAL_MHASHES_MAX_NUM; i++) {
		g_local_mhashes_dones[i] = 0;
	}
	g_local_mhashes_index = 0;
	g_max_fan = 0;
	g_max_temp = 0;
	total_getworks = 0;
	total_accepted = 0;
	total_rejected = 0;
	hw_errors = 0;
	total_stale = 0;
	total_discarded = 0;
	local_work = 0;
	total_go = 0;
	total_ro = 0;
	total_secs = 1.0;
	last_total_secs = 1.0;
	total_diff1 = 0;
	found_blocks = 0;
	total_diff_accepted = 0;
	total_diff_rejected = 0;
	total_diff_stale = 0;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->getwork_requested = 0;
		pool->accepted = 0;
		pool->rejected = 0;
		pool->stale_shares = 0;
		pool->discarded_work = 0;
		pool->getfail_occasions = 0;
		pool->remotefail_occasions = 0;
		pool->last_share_time = 0;
		pool->diff1 = 0;
		pool->diff_accepted = 0;
		pool->diff_rejected = 0;
		pool->diff_stale = 0;
		pool->last_share_diff = 0;
	}

	zero_bestshare();

	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = get_devices(i);

		copy_time(&cgpu->dev_start_tv, &total_tv_start);

		mutex_lock(&hash_lock);
		cgpu->total_mhashes = 0;
		cgpu->accepted = 0;
		cgpu->rejected = 0;
		cgpu->hw_errors = 0;
		cgpu->utility = 0.0;
		cgpu->last_share_pool_time = 0;
		cgpu->diff1 = 0;
		cgpu->diff_accepted = 0;
		cgpu->diff_rejected = 0;
		cgpu->last_share_diff = 0;
		mutex_unlock(&hash_lock);

		/* Don't take any locks in the driver zero stats function, as
		 * it's called async from everything else and we don't want to
		 * deadlock. */
		cgpu->drv->zero_stats(cgpu);
	}
}

static void set_highprio(void)
{
#ifndef WIN32
	int ret = nice(-10);

	if (!ret)
		applog(LOG_DEBUG, "Unable to set thread to high priority");
#else
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

static void set_lowprio(void)
{
#ifndef WIN32
	int ret = nice(10);

	if (!ret)
		applog(LOG_INFO, "Unable to set thread to low priority");
#else
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
}

void default_save_file(char *filename)
{
	if (default_config && *default_config) {
		strcpy(filename, default_config);
		return;
	}

#if defined(unix) || defined(__APPLE__)
	if (getenv("HOME") && *getenv("HOME")) {
	        strcpy(filename, getenv("HOME"));
		strcat(filename, "/");
	}
	else
		strcpy(filename, "");
	strcat(filename, ".cgminer/");
	mkdir(filename, 0777);
#else
	strcpy(filename, "");
#endif
	strcat(filename, def_conf);
}

static void *api_thread(void *userdata)
{
	struct thr_info *mythr = userdata;

	pthread_detach(pthread_self());
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("API");

	set_lowprio();
	api(api_thr_id);

	PTH(mythr) = 0L;

	return NULL;
}

/* Sole work devices are serialised wrt calling get_work so they report in on
 * each pass through their scanhash function as well as in get_work whereas
 * queued work devices work asynchronously so get them to report in and out
 * only across get_work. */
static void thread_reportin(struct thr_info *thr)
{
	thr->getwork = false;
	cgtime(&thr->last);
	thr->cgpu->status = LIFE_WELL;
	thr->cgpu->device_last_well = time(NULL);
}

/* Tell the watchdog thread this thread is waiting on get work and should not
 * be restarted */
static void thread_reportout(struct thr_info *thr)
{
	thr->getwork = true;
	cgtime(&thr->last);
	thr->cgpu->status = LIFE_WELL;
	thr->cgpu->device_last_well = time(NULL);
}

static void hashmeter(int thr_id, uint64_t hashes_done)
{
	bool showlog = false;
	double tv_tdiff;
	time_t now_t;
	int diff_t;

	uint64_t local_mhashes_done = 0;
	uint64_t local_mhashes_done_avg = 0;
	int local_mhashes_done_count = 0;
	int i = 0;

	cgtime(&total_tv_end);
	tv_tdiff = tdiff(&total_tv_end, &tv_hashmeter);
	now_t = total_tv_end.tv_sec;
	diff_t = now_t - hashdisplay_t;
	if (diff_t >= opt_log_interval) {
		alt_status ^= switch_status;
		hashdisplay_t = now_t;
		showlog = true;
	} else if (thr_id < 0) {
		/* hashmeter is called by non-mining threads in case nothing
		 * has reported in to allow hashrate to converge to zero , but
		 * we only update if it has been more than opt_log_interval */
		return;
	}
	copy_time(&tv_hashmeter, &total_tv_end);

	if (thr_id >= 0) {
		struct thr_info *thr = get_thread(thr_id);
		struct cgpu_info *cgpu = thr->cgpu;
		double device_tdiff, thr_mhs;

		/* Update the last time this thread reported in */
		copy_time(&thr->last, &total_tv_end);
		cgpu->device_last_well = now_t;
		device_tdiff = tdiff(&total_tv_end, &cgpu->last_message_tv);
		copy_time(&cgpu->last_message_tv, &total_tv_end);
		thr_mhs = (double)hashes_done / device_tdiff / 1000000;
		applog(LOG_DEBUG, "[thread %d: %"PRIu64" hashes, %.1f mhash/sec]",
		       thr_id, hashes_done, thr_mhs);
		hashes_done /= 1000000;

		mutex_lock(&hash_lock);
		cgpu->total_mhashes += hashes_done;
		decay_time(&cgpu->rolling, hashes_done, device_tdiff, opt_log_interval);
		decay_time(&cgpu->rolling1, hashes_done, device_tdiff, 60.0);
		decay_time(&cgpu->rolling5, hashes_done, device_tdiff, 300.0);
		decay_time(&cgpu->rolling15, hashes_done, device_tdiff, 900.0);
		mutex_unlock(&hash_lock);

		if (want_per_device_stats && showlog) {
			char logline[256];

			get_statline(logline, sizeof(logline), cgpu);
			if (!curses_active) {
				printf("%s          \r", logline);
				fflush(stdout);
			} else
				applog(LOG_INFO, "%s", logline);
		}
	} else {
		/* No device has reported in, we have been called from the
		 * watchdog thread so decay all the hashrates */
		mutex_lock(&hash_lock);
		for (thr_id = 0; thr_id < mining_threads; thr_id++) {
			struct thr_info *thr = get_thread(thr_id);
			struct cgpu_info *cgpu = thr->cgpu;
			double device_tdiff  = tdiff(&total_tv_end, &cgpu->last_message_tv);

			copy_time(&cgpu->last_message_tv, &total_tv_end);
			decay_time(&cgpu->rolling, 0, device_tdiff, opt_log_interval);
			decay_time(&cgpu->rolling1, 0, device_tdiff, 60.0);
			decay_time(&cgpu->rolling5, 0, device_tdiff, 300.0);
			decay_time(&cgpu->rolling15, 0, device_tdiff, 900.0);
		}
		mutex_unlock(&hash_lock);
	}

	mutex_lock(&hash_lock);
	total_mhashes_done += hashes_done;
	if(showlog){		
		g_local_mhashes_index++;
		if(g_local_mhashes_index >= CG_LOCAL_MHASHES_MAX_NUM)
			g_local_mhashes_index = 0;

		for(i = 0; i < CG_LOCAL_MHASHES_MAX_NUM; i++) {
			if(g_local_mhashes_dones[i] >= 0) {
				local_mhashes_done_avg += g_local_mhashes_dones[i];
					//applog(LOG_DEBUG, "g_local_mhashes_dones[%d] = %f,%d", i, g_local_mhashes_dones[i],g_local_mhashes_index);
				local_mhashes_done_count++;
			}
		}
			
			if(local_mhashes_done_count > 0) {
			local_mhashes_done = local_mhashes_done_avg / local_mhashes_done_count;
		} else {
			local_mhashes_done = hashes_done;
		}
			
		decay_time(&total_rolling, local_mhashes_done, opt_log_interval, opt_log_interval);
		decay_time(&rolling1, hashes_done, tv_tdiff, 60.0);
		decay_time(&rolling5, hashes_done,tv_tdiff, 300.0);
		decay_time(&rolling15, hashes_done, tv_tdiff, 900.0);
		global_hashrate = llround(total_rolling) * 1000000;
		g_local_mhashes_dones[g_local_mhashes_index] = 0;
	}
		g_local_mhashes_dones[g_local_mhashes_index] += hashes_done;
	total_secs = tdiff(&total_tv_end, &total_tv_start);

	if(total_secs - last_total_secs > 86400) {
		applog(LOG_ERR, "cgminer time error total_secs = %d last_total_secs = %d", total_secs, last_total_secs);
		mutex_unlock(&hash_lock);
		zero_stats();
		mutex_lock(&hash_lock);
	} else {
		last_total_secs = total_secs;
	}
	if (showlog) {
		char displayed_hashes[16], displayed_rolling[16];
		char displayed_r1[16], displayed_r5[16], displayed_r15[16];
		uint64_t d64;

		d64 = (double)total_mhashes_done / total_secs * 1000000ull;
		suffix_string(d64, displayed_hashes, sizeof(displayed_hashes), 4);
		d64 = (double)total_rolling * 1000000ull;
		g_displayed_rolling = total_rolling / 1000.0;
		suffix_string(d64, displayed_rolling, sizeof(displayed_rolling), 4);
		d64 = (double)rolling1 * 1000000ull;
		suffix_string(d64, displayed_r1, sizeof(displayed_rolling), 4);
		d64 = (double)rolling5 * 1000000ull;
		suffix_string(d64, displayed_r5, sizeof(displayed_rolling), 4);
		d64 = (double)rolling15 * 1000000ull;
		suffix_string(d64, displayed_r15, sizeof(displayed_rolling), 4);

		snprintf(statusline, sizeof(statusline),
			"(%ds):%s (1m):%s (5m):%s (15m):%s (avg):%sh/s",
			opt_log_interval, displayed_rolling, displayed_r1, displayed_r5,
			displayed_r15, displayed_hashes);
	}
	mutex_unlock(&hash_lock);

	if (showlog) {
		if (!curses_active) {
			printf("%s          \r", statusline);
			fflush(stdout);
		} else
			applog(LOG_INFO, "%s", statusline);
	}
}

static void stratum_share_result(json_t *val, json_t *res_val, json_t *err_val,
				 struct stratum_share *sshare)
{
	struct work *work = sshare->work;
	time_t now_t = time(NULL);
	char hashshow[64];
	int srdiff;

	srdiff = now_t - sshare->sshare_sent;
	if (opt_debug || srdiff > 0) {
		applog(LOG_INFO, "Pool %d stratum share result lag time %d seconds",
		       work->pool->pool_no, srdiff);
	}
	show_hash(work, hashshow);
	share_result(val, res_val, err_val, work, hashshow, false, "");
}

/* Parses stratum json responses and tries to find the id that the request
 * matched to and treat it accordingly. */
static bool parse_stratum_response(struct pool *pool, char *s)
{
	json_t *val = NULL, *err_val, *res_val, *id_val;
	struct stratum_share *sshare = NULL;
	json_error_t err;
	bool ret = false;
	int id;

	val = JSON_LOADS(s, &err);
	if (!val) {
		applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);
		goto out;
	}

	res_val = json_object_get(val, "result");
	err_val = json_object_get(val, "error");
	id_val = json_object_get(val, "id");

	if (json_is_null(id_val) || !id_val) {
		char *ss;

		if (err_val)
			ss = json_dumps(err_val, JSON_INDENT(3));
		else
			ss = strdup("(unknown reason)");

		applog(LOG_INFO, "JSON-RPC non method decode failed: %s", ss);

		free(ss);

		goto out;
	}

	id = json_integer_value(id_val);

	mutex_lock(&sshare_lock);
	HASH_FIND_INT(stratum_shares, &id, sshare);
	if (sshare) {
		HASH_DEL(stratum_shares, sshare);
		pool->sshares--;
	}
	mutex_unlock(&sshare_lock);

	if (!sshare) {
		double pool_diff;

		if (!res_val)
			goto out;
		/* Since the share is untracked, we can only guess at what the
		 * work difficulty is based on the current pool diff. */
		cg_rlock(&pool->data_lock);
		pool_diff = pool->sdiff;
		cg_runlock(&pool->data_lock);

		if (json_is_true(res_val)) {
			applog(LOG_NOTICE, "Accepted untracked stratum share from pool %d", pool->pool_no);

			/* We don't know what device this came from so we can't
			 * attribute the work to the relevant cgpu */
			mutex_lock(&stats_lock);
			total_accepted++;
			pool->accepted++;
			total_diff_accepted += pool_diff;
			pool->diff_accepted += pool_diff;
			mutex_unlock(&stats_lock);
		} else {
			applog(LOG_NOTICE, "Rejected untracked stratum share from pool %d", pool->pool_no);

			mutex_lock(&stats_lock);
			total_rejected++;
			pool->rejected++;
			total_diff_rejected += pool_diff;
			pool->diff_rejected += pool_diff;
			mutex_unlock(&stats_lock);
		}
		goto out;
	}
	stratum_share_result(val, res_val, err_val, sshare);
	free_work(sshare->work);
	free(sshare);

	ret = true;
out:
	if (val)
		json_decref(val);

	return ret;
}

void clear_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	double diff_cleared = 0;
	int cleared = 0;

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work->pool == pool) {
			HASH_DEL(stratum_shares, sshare);
			diff_cleared += sshare->work->work_difficulty;
			free_work(sshare->work);
			pool->sshares--;
			free(sshare);
			cleared++;
		}
	}
	mutex_unlock(&sshare_lock);

	if (cleared) {
		applog(LOG_WARNING, "Lost %d shares due to stratum disconnect on pool %d", cleared, pool->pool_no);
		pool->stale_shares += cleared;
		total_stale += cleared;
		pool->diff_stale += diff_cleared;
		total_diff_stale += diff_cleared;
	}
}

void clear_pool_work(struct pool *pool)
{
	struct work *work, *tmp;
	int cleared = 0;

	mutex_lock(stgd_lock);
	HASH_ITER(hh, staged_work, work, tmp) {
		if (work->pool == pool) {
			HASH_DEL(staged_work, work);
			free_work(work);
			cleared++;
		}
	}
	mutex_unlock(stgd_lock);

	if (cleared)
		applog(LOG_INFO, "Cleared %d work items due to stratum disconnect on pool %d", cleared, pool->pool_no);
}

static int cp_prio(void)
{
	int prio;

	cg_rlock(&control_lock);
	prio = currentpool->prio;
	cg_runlock(&control_lock);

	return prio;
}

/* We only need to maintain a secondary pool connection when we need the
 * capacity to get work from the backup pools while still on the primary */
static bool cnx_needed(struct pool *pool)
{
	struct pool *cp;

	if (pool->enabled != POOL_ENABLED)
		return false;

	/* Balance strategies need all pools online */
	if (pool_strategy == POOL_BALANCE)
		return true;
	if (pool_strategy == POOL_LOADBALANCE)
		return true;

	/* Idle stratum pool needs something to kick it alive again */
	if (pool->has_stratum && pool->idle)
		return true;

	/* Getwork pools without opt_fail_only need backup pools up to be able
	 * to leak shares */
	cp = current_pool();
	if (cp == pool)
		return true;
	if (!pool_localgen(cp) && (!opt_fail_only || !cp->hdr_path))
		return true;
	/* If we're waiting for a response from shares submitted, keep the
	 * connection open. */
	if (pool->sshares)
		return true;
	/* If the pool has only just come to life and is higher priority than
	 * the current pool keep the connection open so we can fail back to
	 * it. */
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		return true;
	/* We've run out of work, bring anything back to life. */
	if (no_work)
		return true;
	return false;
}

static void wait_lpcurrent(struct pool *pool);
static void pool_resus(struct pool *pool);
static void gen_stratum_work(struct pool *pool, struct work *work);

void stratum_resumed(struct pool *pool)
{
	if (pool_tclear(pool, &pool->idle)) {
		applog(LOG_INFO, "Stratum connection to pool %d resumed", pool->pool_no);
		pool_resus(pool);
	}
}

static bool supports_resume(struct pool *pool)
{
	bool ret;

	cg_rlock(&pool->data_lock);
	ret = (pool->sessionid != NULL);
	cg_runlock(&pool->data_lock);

	return ret;
}

/* One stratum receive thread per pool that has stratum waits on the socket
 * checking for new messages and for the integrity of the socket connection. We
 * reset the connection based on the integrity of the receive side only as the
 * send side will eventually expire data it fails to send. */
static void *stratum_rthread(void *userdata)
{
	struct pool *pool = (struct pool *)userdata;
	char threadname[16];

	pthread_detach(pthread_self());

	snprintf(threadname, sizeof(threadname), "%d/RStratum", pool->pool_no);
	RenameThread(threadname);

	while (42) {
		struct timeval timeout;
		int sel_ret;
		fd_set rd;
		char *s;

		if (unlikely(pool->removed)) {
			suspend_stratum(pool);
			break;
		}

		/* Check to see whether we need to maintain this connection
		 * indefinitely or just bring it up when we switch to this
		 * pool */
		if (!sock_full(pool) && !cnx_needed(pool)) {
			suspend_stratum(pool);
			clear_stratum_shares(pool);
			clear_pool_work(pool);

			wait_lpcurrent(pool);
			while (!restart_stratum(pool)) {
				if (pool->removed)
					goto out;
				if (enabled_pools > 1)
				cgsleep_ms(30000);
				else
					cgsleep_ms(3000);
			}
		}

		FD_ZERO(&rd);
		FD_SET(pool->sock, &rd);
		timeout.tv_sec = 90;
		timeout.tv_usec = 0;

		/* The protocol specifies that notify messages should be sent
		 * every minute so if we fail to receive any for 90 seconds we
		 * assume the connection has been dropped and treat this pool
		 * as dead */
		if (!sock_full(pool) && (sel_ret = select(pool->sock + 1, &rd, NULL, NULL, &timeout)) < 1) {
			applog(LOG_DEBUG, "Stratum select failed on pool %d with value %d", pool->pool_no, sel_ret);
			s = NULL;
		} else
			s = recv_line(pool);
		if (!s) {
			applog(LOG_NOTICE, "Stratum connection to pool %d interrupted", pool->pool_no);
			pool->getfail_occasions++;
			total_go++;

			/* If the socket to our stratum pool disconnects, all
			 * tracked submitted shares are lost and we will leak
			 * the memory if we don't discard their records. */
			if (!supports_resume(pool) || opt_lowmem)
				clear_stratum_shares(pool);
			clear_pool_work(pool);
			if (pool == current_pool())
				restart_threads();

			while (!restart_stratum(pool)) {
				if (pool->removed)
					goto out;
				cgsleep_ms(30000);
			}
			continue;
		}

		/* Check this pool hasn't died while being a backup pool and
		 * has not had its idle flag cleared */
		stratum_resumed(pool);

		if (!parse_method(pool, s) && !parse_stratum_response(pool, s))
			applog(LOG_INFO, "Unknown stratum msg: %s", s);
		else if (pool->swork.clean) {
			struct work *work = make_work();

			/* Generate a single work item to update the current
			 * block database */
			pool->swork.clean = false;
			gen_stratum_work(pool, work);
			work->longpoll = true;
			/* Return value doesn't matter. We're just informing
			 * that we may need to restart. */
			test_work_current(work);
			free_work(work);
		}
		free(s);
	}

out:
	return NULL;
}

/* Each pool has one stratum send thread for sending shares to avoid many
 * threads being created for submission since all sends need to be serialised
 * anyway. */
static void *stratum_sthread(void *userdata)
{
	struct pool *pool = (struct pool *)userdata;
	uint64_t last_nonce2 = 0;
	uint32_t last_nonce = 0;
	char threadname[16];

	pthread_detach(pthread_self());

	snprintf(threadname, sizeof(threadname), "%d/SStratum", pool->pool_no);
	RenameThread(threadname);

	pool->stratum_q = tq_new();
	if (!pool->stratum_q)
		quit(1, "Failed to create stratum_q in stratum_sthread");

	while (42) {
		char noncehex[12], nonce2hex[20], s[1024];
		struct stratum_share *sshare;
		uint32_t *hash32, nonce;
		unsigned char nonce2[8];
		uint64_t *nonce2_64;
		struct work *work;
		bool submitted;

		if (unlikely(pool->removed))
			break;

		work = tq_pop(pool->stratum_q, NULL);
		if (unlikely(!work))
			quit(1, "Stratum q returned empty work");

		if (unlikely(work->nonce2_len > 8)) {
			applog(LOG_ERR, "Pool %d asking for inappropriately long nonce2 length %d",
			       pool->pool_no, (int)work->nonce2_len);
			applog(LOG_ERR, "Not attempting to submit shares");
			free_work(work);
			continue;
		}

		nonce = *((uint32_t *)(work->data + 76));
		nonce2_64 = (uint64_t *)nonce2;
		*nonce2_64 = htole64(work->nonce2);
		/* Filter out duplicate shares */
		if (unlikely(nonce == last_nonce && *nonce2_64 == last_nonce2)) {
			applog(LOG_INFO, "Filtering duplicate share to pool %d",
			       pool->pool_no);
			free_work(work);
			continue;
		}
		last_nonce = nonce;
		last_nonce2 = *nonce2_64;
		__bin2hex(noncehex, (const unsigned char *)&nonce, 4);
		__bin2hex(nonce2hex, nonce2, work->nonce2_len);

		sshare = calloc(sizeof(struct stratum_share), 1);
		hash32 = (uint32_t *)work->hash;
		submitted = false;

		sshare->sshare_time = time(NULL);
		/* This work item is freed in parse_stratum_response */
		sshare->work = work;
		memset(s, 0, 1024);

		mutex_lock(&sshare_lock);
		/* Give the stratum share a unique id */
		sshare->id = swork_id++;
		mutex_unlock(&sshare_lock);

		snprintf(s, sizeof(s),
			"{\"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\"], \"id\": %d, \"method\": \"mining.submit\"}",
			pool->rpc_user, work->job_id, nonce2hex, work->ntime, noncehex, sshare->id);

		applog(LOG_INFO, "Submitting share %08lx to pool %d",
					(long unsigned int)htole32(hash32[6]), pool->pool_no);

		/* Try resubmitting for up to 2 minutes if we fail to submit
		 * once and the stratum pool nonce1 still matches suggesting
		 * we may be able to resume. */
		while (time(NULL) < sshare->sshare_time + 120) {
			bool sessionid_match;

			if (likely(stratum_send(pool, s, strlen(s)))) {
				mutex_lock(&sshare_lock);
				HASH_ADD_INT(stratum_shares, id, sshare);
				pool->sshares++;
				mutex_unlock(&sshare_lock);

				if (pool_tclear(pool, &pool->submit_fail))
						applog(LOG_WARNING, "Pool %d communication resumed, submitting work", pool->pool_no);
				applog(LOG_DEBUG, "Successfully submitted, adding to stratum_shares db");
				submitted = true;
				break;
			}
			if (!pool_tset(pool, &pool->submit_fail) && cnx_needed(pool)) {
				applog(LOG_WARNING, "Pool %d stratum share submission failure", pool->pool_no);
				total_ro++;
				pool->remotefail_occasions++;
			}

			if (opt_lowmem) {
				applog(LOG_DEBUG, "Lowmem option prevents resubmitting stratum share");
				break;
			}

			cg_rlock(&pool->data_lock);
			sessionid_match = (pool->nonce1 && !strcmp(work->nonce1, pool->nonce1));
			cg_runlock(&pool->data_lock);

			if (!sessionid_match) {
				applog(LOG_DEBUG, "No matching session id for resubmitting stratum share");
				break;
			}
			/* Retry every 5 seconds */
			sleep(5);
		}

		if (unlikely(!submitted)) {
			applog(LOG_DEBUG, "Failed to submit stratum share, discarding");
			free_work(work);
			free(sshare);
			pool->stale_shares++;
			total_stale++;
		} else {
			int ssdiff;

			sshare->sshare_sent = time(NULL);
			ssdiff = sshare->sshare_sent - sshare->sshare_time;
			if (opt_debug || ssdiff > 0) {
				applog(LOG_INFO, "Pool %d stratum share submission lag time %d seconds",
				       pool->pool_no, ssdiff);
			}
		}
	}

	/* Freeze the work queue but don't free up its memory in case there is
	 * work still trying to be submitted to the removed pool. */
	tq_freeze(pool->stratum_q);

	return NULL;
}

static void init_stratum_threads(struct pool *pool)
{
	have_longpoll = true;

	if (unlikely(pthread_create(&pool->stratum_sthread, NULL, stratum_sthread, (void *)pool)))
		quit(1, "Failed to create stratum sthread");
	if (unlikely(pthread_create(&pool->stratum_rthread, NULL, stratum_rthread, (void *)pool)))
		quit(1, "Failed to create stratum rthread");
}

static void *longpoll_thread(void *userdata);

static bool stratum_works(struct pool *pool)
{
	applog(LOG_INFO, "Testing pool %d stratum %s", pool->pool_no, pool->stratum_url);
	check_extranonce_option(pool, pool->stratum_url);
	if (!extract_sockaddr(pool->stratum_url, &pool->sockaddr_url, &pool->stratum_port))
		return false;

	if (!initiate_stratum(pool))
		return false;

	return true;
}

static bool setup_gbt_solo(CURL __maybe_unused *curl, struct pool __maybe_unused *pool)
{
	return false;
}


static void pool_start_lp(struct pool *pool)
{
	if (!pool->lp_started) {
		pool->lp_started = true;
		if (unlikely(pthread_create(&pool->longpoll_thread, NULL, longpoll_thread, (void *)pool)))
			quit(1, "Failed to create pool longpoll thread");
	}
}

static bool pool_active(struct pool *pool, bool pinging)
{
	struct timeval tv_getwork, tv_getwork_reply;
	json_t *val = NULL;
	bool ret = false;
	CURL *curl;
	int uninitialised_var(rolltime);

	if (pool->has_gbt)
		applog(LOG_DEBUG, "Retrieving block template from pool %s", pool->rpc_url);
	else
		applog(LOG_INFO, "Testing pool %s", pool->rpc_url);

	/* This is the central point we activate stratum when we can */
retry_stratum:
	if (pool->has_stratum) {
		/* We create the stratum thread for each pool just after
		 * successful authorisation. Once the init flag has been set
		 * we never unset it and the stratum thread is responsible for
		 * setting/unsetting the active flag */
		bool init = pool_tset(pool, &pool->stratum_init);

		if (!init) {
			bool ret = initiate_stratum(pool) && auth_stratum(pool);
			extranonce_subscribe_stratum(pool);
			if (ret)
				init_stratum_threads(pool);
			else
				pool_tclear(pool, &pool->stratum_init);
			return ret;
		}
		return pool->stratum_active;
	}

	curl = curl_easy_init();
	if (unlikely(!curl)) {
		applog(LOG_ERR, "CURL initialisation failed");
		return false;
	}

	/* Probe for GBT support on first pass */
	if (!pool->probed) {
		applog(LOG_DEBUG, "Probing for GBT support");
		val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
				    gbt_req, true, false, &rolltime, pool, false);
		if (val) {
			bool append = false, submit = false, transactions = false;
			json_t *res_val, *mutables;
			int i, mutsize = 0;

			res_val = json_object_get(val, "result");
			if (res_val) {
				mutables = json_object_get(res_val, "mutable");
				mutsize = json_array_size(mutables);
			}

			for (i = 0; i < mutsize; i++) {
				json_t *arrval = json_array_get(mutables, i);

				if (json_is_string(arrval)) {
					const char *mutable = json_string_value(arrval);

					if (!strncasecmp(mutable, "coinbase/append", 15))
						append = true;
					else if (!strncasecmp(mutable, "submit/coinbase", 15))
						submit = true;
					else if (!strncasecmp(mutable, "transactions", 12))
						transactions = true;
				}
			}
			json_decref(val);

			/* Only use GBT if it supports coinbase append and
			 * submit coinbase */
			if (append && submit) {
				pool->has_gbt = true;
				pool->rpc_req = gbt_req;
			} else if (transactions) {
				pool->gbt_solo = true;
				pool->rpc_req = gbt_solo_req;
			}
		}
		/* Reset this so we can probe fully just after this. It will be
		 * set to true that time.*/
		pool->probed = false;

		if (pool->has_gbt)
			applog(LOG_DEBUG, "GBT coinbase + append support found, switching to GBT protocol");
		else if (pool->gbt_solo)
			applog(LOG_DEBUG, "GBT coinbase without append found, switching to GBT solo protocol");
		else
			applog(LOG_DEBUG, "No GBT coinbase + append support found, using getwork protocol");
	}

	cgtime(&tv_getwork);
	val = json_rpc_call(curl, pool->rpc_url, pool->rpc_userpass,
			    pool->rpc_req, true, false, &rolltime, pool, false);
	cgtime(&tv_getwork_reply);

	/* Detect if a http getwork pool has an X-Stratum header at startup,
	 * and if so, switch to that in preference to getwork if it works */
	if (pool->stratum_url && !opt_fix_protocol && stratum_works(pool)) {
		applog(LOG_NOTICE, "Switching pool %d %s to %s", pool->pool_no, pool->rpc_url, pool->stratum_url);
		if (!pool->rpc_url)
			pool->rpc_url = strdup(pool->stratum_url);
		pool->has_stratum = true;
		curl_easy_cleanup(curl);

		goto retry_stratum;
	}

	if (val) {
		struct work *work = make_work();
		bool rc;

		rc = work_decode(pool, work, val);
		if (rc) {
			if (pool->gbt_solo) {
				ret = setup_gbt_solo(curl, pool);
				if (ret)
					pool_start_lp(pool);
				free_work(work);
				goto out;
			}
			applog(LOG_DEBUG, "Successfully retrieved and deciphered work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			work->pool = pool;
			work->rolltime = rolltime;
			copy_time(&work->tv_getwork, &tv_getwork);
			copy_time(&work->tv_getwork_reply, &tv_getwork_reply);
			work->getwork_mode = GETWORK_MODE_TESTPOOL;
			calc_diff(work, 0);
			applog(LOG_DEBUG, "Pushing pooltest work to base pool");

			stage_work(work);
			total_getworks++;
			pool->getwork_requested++;
			ret = true;
		} else {
			applog(LOG_DEBUG, "Successfully retrieved but FAILED to decipher work from pool %u %s",
			       pool->pool_no, pool->rpc_url);
			free_work(work);
		}

		if (pool->lp_url)
			goto out;

		/* Decipher the longpoll URL, if any, and store it in ->lp_url */
		if (pool->hdr_path) {
			char *copy_start, *hdr_path;
			bool need_slash = false;
			size_t siz;

			hdr_path = pool->hdr_path;
			if (strstr(hdr_path, "://")) {
				pool->lp_url = hdr_path;
				hdr_path = NULL;
			} else {
				/* absolute path, on current server */
				copy_start = (*hdr_path == '/') ? (hdr_path + 1) : hdr_path;
				if (pool->rpc_url[strlen(pool->rpc_url) - 1] != '/')
					need_slash = true;

				siz = strlen(pool->rpc_url) + strlen(copy_start) + 2;
				pool->lp_url = malloc(siz);
				if (!pool->lp_url) {
					applog(LOG_ERR, "Malloc failure in pool_active");
					return false;
				}

				snprintf(pool->lp_url, siz, "%s%s%s", pool->rpc_url, need_slash ? "/" : "", copy_start);
			}
		} else
			pool->lp_url = NULL;

		pool_start_lp(pool);
	} else {
		/* If we failed to parse a getwork, this could be a stratum
		 * url without the prefix stratum+tcp:// so let's check it */
		if (initiate_stratum(pool)) {
			pool->has_stratum = true;
			goto retry_stratum;
		}
		applog(LOG_DEBUG, "FAILED to retrieve work from pool %u %s",
		       pool->pool_no, pool->rpc_url);
		if (!pinging && !pool->idle)
			applog(LOG_WARNING, "Pool %u slow/down or URL or credentials invalid", pool->pool_no);
	}
out:
	if (val)
		json_decref(val);
	curl_easy_cleanup(curl);
	return ret;
}

static void pool_resus(struct pool *pool)
{
	pool->seq_getfails = 0;
	if (pool_strategy == POOL_FAILOVER && pool->prio < cp_prio())
		applog(LOG_WARNING, "Pool %d %s alive, testing stability", pool->pool_no, pool->rpc_url);
	else
		applog(LOG_INFO, "Pool %d %s alive", pool->pool_no, pool->rpc_url);
}

static bool work_filled;
static bool work_emptied;

/* If this is called non_blocking, it will return NULL for work so that must
 * be handled. */
static struct work *hash_pop(bool blocking)
{
	struct work *work = NULL, *tmp;
	int hc;

	mutex_lock(stgd_lock);
	if (!HASH_COUNT(staged_work)) {
		/* Increase the queue if we reach zero and we know we can reach
		 * the maximum we're asking for. */
		if (work_filled && max_queue < opt_queue) {
			max_queue++;
			work_filled = false;
		}
		work_emptied = true;
		if (!blocking)
			goto out_unlock;
		do {
			struct timespec then;
			struct timeval now;
			int rc;

			cgtime(&now);
			then.tv_sec = now.tv_sec + 10;
			then.tv_nsec = now.tv_usec * 1000;
			pthread_cond_signal(&gws_cond);
			rc = pthread_cond_timedwait(&getq->cond, stgd_lock, &then);
			/* Check again for !no_work as multiple threads may be
				* waiting on this condition and another may set the
				* bool separately. */
			if (rc && !no_work) {
				no_work = true;
				applog(LOG_WARNING, "Waiting for work to be available from pools.");
			}
		} while (!HASH_COUNT(staged_work));
	}

	if (no_work) {
		applog(LOG_WARNING, "Work available from pools, resuming.");
		no_work = false;
	}

	hc = HASH_COUNT(staged_work);
	/* Find clone work if possible, to allow masters to be reused */
	if (hc > staged_rollable) {
		HASH_ITER(hh, staged_work, work, tmp) {
			if (!work_rollable(work))
				break;
		}
	} else
		work = staged_work;
	HASH_DEL(staged_work, work);
	if (work_rollable(work))
		staged_rollable--;

	/* Signal the getwork scheduler to look for more work */
	pthread_cond_signal(&gws_cond);

	/* Signal hash_pop again in case there are mutliple hash_pop waiters */
	pthread_cond_signal(&getq->cond);

	/* Keep track of last getwork grabbed */
	last_getwork = time(NULL);
out_unlock:
	mutex_unlock(stgd_lock);

	return work;
}

static void gen_hash(unsigned char *data, unsigned char *hash, int len)
{
	unsigned char hash1[32];

	sha256(data, len, hash1);
	sha256(hash1, 32, hash);
}

void set_target(unsigned char *dest_target, double diff)
{
	unsigned char target[32];
	uint64_t *data64, h64;
	double d64, dcut64;

	if (unlikely(diff == 0.0)) {
		/* This shouldn't happen but best we check to prevent a crash */
		applog(LOG_ERR, "Diff zero passed to set_target");
		diff = 1.0;
	}

	d64 = truediffone;
	d64 /= diff;

	dcut64 = d64 / bits192;
	h64 = dcut64;
	data64 = (uint64_t *)(target + 24);
	*data64 = htole64(h64);
	dcut64 = h64;
	dcut64 *= bits192;
	d64 -= dcut64;

	dcut64 = d64 / bits128;
	h64 = dcut64;
	data64 = (uint64_t *)(target + 16);
	*data64 = htole64(h64);
	dcut64 = h64;
	dcut64 *= bits128;
	d64 -= dcut64;

	dcut64 = d64 / bits64;
	h64 = dcut64;
	data64 = (uint64_t *)(target + 8);
	*data64 = htole64(h64);
	dcut64 = h64;
	dcut64 *= bits64;
	d64 -= dcut64;

	h64 = d64;
	data64 = (uint64_t *)(target);
	*data64 = htole64(h64);

	if (opt_debug) {
		char *htarget = bin2hex(target, 32);

		applog(LOG_DEBUG, "Generated target %s", htarget);
		free(htarget);
	}
	memcpy(dest_target, target, 32);
}

#if defined (USE_AVALON2) || defined (USE_HASHRATIO)
bool submit_nonce2_nonce(struct thr_info *thr, struct pool *pool, struct pool *real_pool,
			 uint32_t nonce2, uint32_t nonce)
{
	const int thr_id = thr->id;
	struct cgpu_info *cgpu = thr->cgpu;
	struct device_drv *drv = cgpu->drv;
	struct work *work = make_work();
	bool ret;

	cg_wlock(&pool->data_lock);
	pool->nonce2 = nonce2;
	cg_wunlock(&pool->data_lock);

	gen_stratum_work(pool, work);
	work->pool = real_pool;

	work->thr_id = thr_id;
	work->work_block = work_block;
	work->pool->works++;

	work->mined = true;
	work->device_diff = MIN(drv->max_diff, work->work_difficulty);
	work->device_diff = MAX(drv->min_diff, work->device_diff);

	ret = submit_nonce(thr, work, nonce);
	free_work(work);
	return ret;
}
#endif

/* Generates stratum based work based on the most recent notify information
 * from the pool. This will keep generating work while a pool is down so we use
 * other means to detect when the pool has died in stratum_thread */
static void gen_stratum_work(struct pool *pool, struct work *work)
{
	unsigned char merkle_root[32], merkle_sha[64];
	uint32_t *data32, *swap32;
	uint64_t nonce2le;
	int i;

	cg_wlock(&pool->data_lock);

	/* Update coinbase. Always use an LE encoded nonce2 to fill in values
	 * from left to right and prevent overflow errors with small n2sizes */
	nonce2le = htole64(pool->nonce2);
	memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
	work->nonce2 = pool->nonce2++;
	work->nonce2_len = pool->n2size;

	/* Downgrade to a read lock to read off the pool variables */
	cg_dwlock(&pool->data_lock);

	/* Generate merkle root */
	gen_hash(pool->coinbase, merkle_root, pool->coinbase_len);
	memcpy(merkle_sha, merkle_root, 32);
	for (i = 0; i < pool->merkles; i++) {
		memcpy(merkle_sha + 32, pool->swork.merkle_bin[i], 32);
		gen_hash(merkle_sha, merkle_root, 64);
		memcpy(merkle_sha, merkle_root, 32);
	}
	data32 = (uint32_t *)merkle_sha;
	swap32 = (uint32_t *)merkle_root;
	flip32(swap32, data32);

	/* Copy the data template from header_bin */
	memcpy(work->data, pool->header_bin, 112);
	memcpy(work->data + 36, merkle_root, 32);

	/* Store the stratum work diff to check it still matches the pool's
	 * stratum diff when submitting shares */
	work->sdiff = pool->sdiff;

	/* Copy parameters required for share submission */
	work->job_id = strdup(pool->swork.job_id);
	work->nonce1 = strdup(pool->nonce1);
	work->ntime = strdup(pool->ntime);
	cg_runlock(&pool->data_lock);

	if (opt_debug) {
		char *header, *merkle_hash;

		header = bin2hex(work->data, 112);
		merkle_hash = bin2hex((const unsigned char *)merkle_root, 32);
		applog(LOG_DEBUG, "Generated stratum merkle %s", merkle_hash);
		applog(LOG_DEBUG, "Generated stratum header %s", header);
		applog(LOG_DEBUG, "Work job_id %s nonce2 %"PRIu64" ntime %s", work->job_id,
		       work->nonce2, work->ntime);
		free(header);
		free(merkle_hash);
	}

	calc_midstate(work);
	set_target(work->target, work->sdiff);

	local_work++;
	if((time(NULL) - local_work_lasttime) > 5) {
		int diff = local_work - local_work_last;
		applog(LOG_DEBUG, "local_work 5s gen work count:%d", diff/(time(NULL) - local_work_lasttime));
		local_work_lasttime = time(NULL);
		local_work_last = local_work;
	}

	work->pool = pool;
	work->stratum = true;
	work->nonce = 0;
	work->longpoll = false;
	work->getwork_mode = GETWORK_MODE_STRATUM;
	work->work_block = work_block;
	/* Nominally allow a driver to ntime roll 60 seconds */
	work->drv_rolllimit = 60;
	calc_diff(work, work->sdiff);

	cgtime(&work->tv_staged);
}

/* The time difference in seconds between when this device last got work via
 * get_work() and generated a valid share. */
int share_work_tdiff(struct cgpu_info *cgpu)
{
	return last_getwork - cgpu->last_device_valid_work;
}

static void set_benchmark_work(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->lodiff += cgpu->direction;
	if (cgpu->lodiff < 1)
		cgpu->direction = 1;
	if (cgpu->lodiff > 15) {
		cgpu->direction = -1;
		if (++cgpu->hidiff > 15)
			cgpu->hidiff = 0;
		memcpy(work, &bench_hidiff_bins[cgpu->hidiff][0], 160);
	} else
		memcpy(work, &bench_lodiff_bins[cgpu->lodiff][0], 160);
}

struct work *get_work(struct thr_info *thr, const int thr_id)
{
	struct cgpu_info *cgpu = thr->cgpu;
	struct work *work = NULL;
	time_t diff_t;

	thread_reportout(thr);
	applog(LOG_DEBUG, "Popping work from get queue to get work");
	diff_t = time(NULL);
	while (!work) {
		work = hash_pop(true);
		if (stale_work(work, false)) {
			discard_work(work);
			wake_gws();
		}
	}
	diff_t = time(NULL) - diff_t;
	/* Since this is a blocking function, we need to add grace time to
	 * the device's last valid work to not make outages appear to be
	 * device failures. */
	if (diff_t > 0) {
		applog(LOG_DEBUG, "Get work blocked for %d seconds", (int)diff_t);
		cgpu->last_device_valid_work += diff_t;
	}
	applog(LOG_DEBUG, "Got work from get queue to get work for thread %d", thr_id);

	work->thr_id = thr_id;
	if (opt_benchmark)
		set_benchmark_work(cgpu, work);

	thread_reportin(thr);
	work->mined = true;
	work->device_diff = MIN(cgpu->drv->max_diff, work->work_difficulty);
	work->device_diff = MAX(cgpu->drv->min_diff, work->device_diff);
	return work;
}

/* Submit a copy of the tested, statistic recorded work item asynchronously */
static void submit_work_async(struct work *work)
{
	struct pool *pool = work->pool;
	pthread_t submit_thread;

	cgtime(&work->tv_work_found);
	if (opt_benchmark) {
		struct cgpu_info *cgpu = get_thr_cgpu(work->thr_id);

		mutex_lock(&stats_lock);
		cgpu->accepted++;
		total_accepted++;
		pool->accepted++;
		cgpu->diff_accepted += work->work_difficulty;
		total_diff_accepted += work->work_difficulty;
		pool->diff_accepted += work->work_difficulty;
		mutex_unlock(&stats_lock);

		applog(LOG_NOTICE, "Accepted %s %d benchmark share nonce %08x",
		       cgpu->drv->name, cgpu->device_id, *(uint32_t *)(work->data + 64 + 12));
		return;
	}

	if (stale_work(work, true)) {
		if (opt_submit_stale)
			applog(LOG_NOTICE, "Pool %d stale share detected, submitting as user requested", pool->pool_no);
		else if (pool->submit_old)
			applog(LOG_NOTICE, "Pool %d stale share detected, submitting as pool requested", pool->pool_no);
		else {
			applog(LOG_NOTICE, "Pool %d stale share detected, discarding", pool->pool_no);
			sharelog("discard", work);

			mutex_lock(&stats_lock);
			total_stale++;
			pool->stale_shares++;
			total_diff_stale += work->work_difficulty;
			pool->diff_stale += work->work_difficulty;
			mutex_unlock(&stats_lock);

			free_work(work);
			return;
		}
		work->stale = true;
	}

	if (work->stratum) {
		applog(LOG_DEBUG, "Pushing pool %d work to stratum queue", pool->pool_no);
		if (unlikely(!tq_push(pool->stratum_q, work))) {
			applog(LOG_DEBUG, "Discarding work from removed pool");
			free_work(work);
		}
	} else {
		applog(LOG_DEBUG, "Pushing submit work to work thread");
		if (unlikely(pthread_create(&submit_thread, NULL, submit_work_thread, (void *)work)))
			quit(1, "Failed to create submit_work_thread");
	}
}

void inc_hw_errors(struct thr_info *thr)
{
	applog(LOG_ERR, "%s%d: invalid nonce - HW error", thr->cgpu->drv->name,
	       thr->cgpu->device_id);

	mutex_lock(&stats_lock);
	hw_errors++;
	thr->cgpu->hw_errors++;
	mutex_unlock(&stats_lock);

	thr->cgpu->drv->hw_error(thr);
}

void inc_dev_status(int max_fan, int max_temp)
{
	mutex_lock(&stats_lock);
	g_max_fan = max_fan;
	g_max_temp = max_temp;
	mutex_unlock(&stats_lock);
}

/* Fills in the work nonce and builds the output data in work->hash */
static void rebuild_nonce(struct work *work, uint32_t nonce)
{
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);

	*work_nonce = htole32(nonce);

	regen_hash(work);
}

/* For testing a nonce against diff 1 */
bool test_nonce(struct work *work, uint32_t nonce)
{
	uint32_t *hash_32 = (uint32_t *)(work->hash + 28);

	rebuild_nonce(work, nonce);
	return (*hash_32 == 0);
}

/* For testing a nonce against an arbitrary diff */
bool test_nonce_diff(struct work *work, uint32_t nonce, double diff)
{
	uint64_t *hash64 = (uint64_t *)(work->hash + 24), diff64;

	rebuild_nonce(work, nonce);
	diff64 = 0x00000000ffff0000ULL;
	diff64 /= diff;

	return (le64toh(*hash64) <= diff64);
}

static void update_work_stats(struct thr_info *thr, struct work *work)
{
	double test_diff = current_diff;

	work->share_diff = share_diff(work);

	if (unlikely(work->share_diff >= test_diff)) {
		work->block = true;
		work->pool->solved++;
		found_blocks++;
		work->mandatory = true;
		applog(LOG_NOTICE, "Found block for pool %d!", work->pool->pool_no);
	}

	mutex_lock(&stats_lock);
	total_diff1 += work->device_diff;
	thr->cgpu->diff1 += work->device_diff;
	work->pool->diff1 += work->device_diff;
	thr->cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);
}

void inc_work_stats(struct thr_info *thr, struct pool *pool, int diff1)
{
	mutex_lock(&stats_lock);
	total_diff1 += diff1;
	thr->cgpu->diff1 += diff1;
	if(pool) {
		pool->diff1 += diff1;
	} else {
		pool = current_pool();
		pool->diff1 += diff1;
	}
	thr->cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);
}


/* To be used once the work has been tested to be meet diff1 and has had its
 * nonce adjusted. Returns true if the work target is met. */
bool submit_tested_work(struct thr_info *thr, struct work *work)
{
	struct work *work_out;
	update_work_stats(thr, work);

	if (!fulltest(work->hash, work->target)) {
		applog(LOG_INFO, "%s %d: Share above target", thr->cgpu->drv->name,
		       thr->cgpu->device_id);
		return false;
	}
	work_out = copy_work(work);
	submit_work_async(work_out);
	return true;
}

/* Rudimentary test to see if cgpu has returned the same nonce twice in a row which is
 * always going to be a duplicate which should be reported as a hw error. */
static bool new_nonce(struct thr_info *thr, uint32_t nonce)
{
	struct cgpu_info *cgpu = thr->cgpu;

	if (unlikely(cgpu->last_nonce == nonce)) {
		applog(LOG_INFO, "%s %d duplicate share detected as HW error",
		       cgpu->drv->name, cgpu->device_id);
		return false;
	}
	cgpu->last_nonce = nonce;
	return true;
}

/* Returns true if nonce for work was a valid share and not a dupe of the very last
 * nonce submitted by this device. */
bool submit_nonce(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	if (new_nonce(thr, nonce) && test_nonce(work, nonce))
		submit_tested_work(thr, work);
	else {
		inc_hw_errors(thr);
		return false;
	}

	if (opt_benchfile && opt_benchfile_display)
		benchfile_dspwork(work, nonce);

	return true;
}

bool submit_nonce_1(struct thr_info *thr, struct work *work, uint32_t nonce, int * nofull)
{
	if(nofull) *nofull = 0;
	if (test_nonce(work, nonce)) {
		update_work_stats(thr, work);
		if (!fulltest(work->hash, work->target)) {
			if(nofull) *nofull = 1;
			applog(LOG_INFO, "Share above target");
			return false;
		}
	} else {
		inc_hw_errors(thr);
		return false;
	}
	return true;
}

void submit_nonce_2(struct work *work)
{
	struct work *work_out;
	work_out = copy_work(work);
	submit_work_async(work_out);
}

bool submit_nonce_direct(struct thr_info *thr, struct work *work, uint32_t nonce)
{
	struct work *work_out;
	uint32_t *work_nonce = (uint32_t *)(work->data + 64 + 12);
	*work_nonce = htole32(nonce);

	work_out = copy_work(work);
	submit_work_async(work_out);
	return true;
}

/* Allows drivers to submit work items where the driver has changed the ntime
 * value by noffset. Must be only used with a work protocol that does not ntime
 * roll itself intrinsically to generate work (eg stratum). We do not touch
 * the original work struct, but the copy of it only. */
bool submit_noffset_nonce(struct thr_info *thr, struct work *work_in, uint32_t nonce,
			  int noffset)
{
	struct work *work = make_work();
	bool ret = false;

	_copy_work(work, work_in, noffset);
	if (!test_nonce(work, nonce)) {
		free_work(work);
		inc_hw_errors(thr);
		goto out;
	}
	update_work_stats(thr, work);

	if (opt_benchfile && opt_benchfile_display)
		benchfile_dspwork(work, nonce);

	ret = true;
	if (!fulltest(work->hash, work->target)) {
		free_work(work);
		applog(LOG_INFO, "%s %d: Share above target", thr->cgpu->drv->name,
		       thr->cgpu->device_id);
		goto  out;
	}
	submit_work_async(work);

out:
	return ret;
}

static inline bool abandon_work(struct work *work, struct timeval *wdiff, uint64_t hashes)
{
	if (wdiff->tv_sec > opt_scantime || hashes >= 0xfffffffe ||
	    stale_work(work, false))
		return true;
	return false;
}

static void mt_disable(struct thr_info *mythr, const int thr_id,
		       struct device_drv *drv)
{
	applog(LOG_WARNING, "Thread %d being disabled", thr_id);
	mythr->cgpu->rolling = 0;
	applog(LOG_DEBUG, "Waiting on sem in miner thread");
	cgsem_wait(&mythr->sem);
	applog(LOG_WARNING, "Thread %d being re-enabled", thr_id);
	drv->thread_enable(mythr);
}

/* The main hashing loop for devices that are slow enough to work on one work
 * item at a time, without a queue, aborting work before the entire nonce
 * range has been hashed if needed. */
static void hash_sole_work(struct thr_info *mythr)
{
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	struct timeval getwork_start, tv_start, *tv_end, tv_workstart, tv_lastupdate;
	struct cgminer_stats *dev_stats = &(cgpu->cgminer_stats);
	struct cgminer_stats *pool_stats;
	/* Try to cycle approximately 5 times before each log update */
	const long cycle = opt_log_interval / 5 ? : 1;
	const bool primary = (!mythr->device_thread) || mythr->primary_thread;
	struct timeval diff, sdiff, wdiff = {0, 0};
	uint32_t max_nonce = drv->can_limit_work(mythr);
	int64_t hashes_done = 0;

	tv_end = &getwork_start;
	cgtime(&getwork_start);
	sdiff.tv_sec = sdiff.tv_usec = 0;
	cgtime(&tv_lastupdate);

	while (likely(!cgpu->shutdown)) {
		struct work *work = get_work(mythr, thr_id);
		int64_t hashes;

		mythr->work_restart = false;
		cgpu->new_work = true;

		cgtime(&tv_workstart);
		work->nonce = 0;
		cgpu->max_hashes = 0;
		if (!drv->prepare_work(mythr, work)) {
			applog(LOG_ERR, "work prepare failed, exiting "
				"mining thread %d", thr_id);
			break;
		}
		work->device_diff = MIN(drv->max_diff, work->work_difficulty);
		work->device_diff = MAX(drv->min_diff, work->device_diff);

		do {
			cgtime(&tv_start);

			subtime(&tv_start, &getwork_start);

			addtime(&getwork_start, &dev_stats->getwork_wait);
			if (time_more(&getwork_start, &dev_stats->getwork_wait_max))
				copy_time(&dev_stats->getwork_wait_max, &getwork_start);
			if (time_less(&getwork_start, &dev_stats->getwork_wait_min))
				copy_time(&dev_stats->getwork_wait_min, &getwork_start);
			dev_stats->getwork_calls++;

			pool_stats = &(work->pool->cgminer_stats);

			addtime(&getwork_start, &pool_stats->getwork_wait);
			if (time_more(&getwork_start, &pool_stats->getwork_wait_max))
				copy_time(&pool_stats->getwork_wait_max, &getwork_start);
			if (time_less(&getwork_start, &pool_stats->getwork_wait_min))
				copy_time(&pool_stats->getwork_wait_min, &getwork_start);
			pool_stats->getwork_calls++;

			cgtime(&(work->tv_work_start));

			/* Only allow the mining thread to be cancelled when
			 * it is not in the driver code. */
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

			thread_reportin(mythr);
			hashes = drv->scanhash(mythr, work, work->nonce + max_nonce);
			thread_reportout(mythr);

			pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
			pthread_testcancel();

			/* tv_end is == &getwork_start */
			cgtime(&getwork_start);

			if (unlikely(hashes == -1)) {
				applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
				cgpu->deven = DEV_DISABLED;
				dev_error(cgpu, REASON_THREAD_ZERO_HASH);
				cgpu->shutdown = true;
				break;
			}

			hashes_done += hashes;
			if (hashes > cgpu->max_hashes)
				cgpu->max_hashes = hashes;

			timersub(tv_end, &tv_start, &diff);
			sdiff.tv_sec += diff.tv_sec;
			sdiff.tv_usec += diff.tv_usec;
			if (sdiff.tv_usec > 1000000) {
				++sdiff.tv_sec;
				sdiff.tv_usec -= 1000000;
			}

			timersub(tv_end, &tv_workstart, &wdiff);

			if (unlikely((long)sdiff.tv_sec < cycle)) {
				int mult;

				if (likely(max_nonce == 0xffffffff))
					continue;

				mult = 1000000 / ((sdiff.tv_usec + 0x400) / 0x400) + 0x10;
				mult *= cycle;
				if (max_nonce > (0xffffffff * 0x400) / mult)
					max_nonce = 0xffffffff;
				else
					max_nonce = (max_nonce * mult) / 0x400;
			} else if (unlikely(sdiff.tv_sec > cycle))
				max_nonce = max_nonce * cycle / sdiff.tv_sec;
			else if (unlikely(sdiff.tv_usec > 100000))
				max_nonce = max_nonce * 0x400 / (((cycle * 1000000) + sdiff.tv_usec) / (cycle * 1000000 / 0x400));

			timersub(tv_end, &tv_lastupdate, &diff);
			/* Update the hashmeter at most 5 times per second */
			if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
			    diff.tv_sec >= opt_log_interval) {
				hashmeter(thr_id, hashes_done);
				hashes_done = 0;
				copy_time(&tv_lastupdate, tv_end);
			}

			if (unlikely(mythr->work_restart)) {
				/* Apart from device_thread 0, we stagger the
				 * starting of every next thread to try and get
				 * all devices busy before worrying about
				 * getting work for their extra threads */
				if (!primary) {
					struct timespec rgtp;

					rgtp.tv_sec = 0;
					rgtp.tv_nsec = 250 * mythr->device_thread * 1000000;
					nanosleep(&rgtp, NULL);
				}
				break;
			}

			if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
				mt_disable(mythr, thr_id, drv);

			sdiff.tv_sec = sdiff.tv_usec = 0;
		} while (!abandon_work(work, &wdiff, cgpu->max_hashes));
		free_work(work);
	}
	cgpu->deven = DEV_DISABLED;
}

/* Put a new unqueued work item in cgpu->unqueued_work under cgpu->qlock till
 * the driver tells us it's full so that it may extract the work item using
 * the get_queued() function which adds it to the hashtable on
 * cgpu->queued_work. */
static void fill_queue(struct thr_info *mythr, struct cgpu_info *cgpu, struct device_drv *drv, const int thr_id)
{
	do {
		bool need_work;

		/* Do this lockless just to know if we need more unqueued work. */
		need_work = (!cgpu->unqueued_work);

		/* get_work is a blocking function so do it outside of lock
		 * to prevent deadlocks with other locks. */
		if (need_work) {
			struct work *work = get_work(mythr, thr_id);

			wr_lock(&cgpu->qlock);
			/* Check we haven't grabbed work somehow between
			 * checking and picking up the lock. */
			if (likely(!cgpu->unqueued_work))
				cgpu->unqueued_work = work;
			else
				need_work = false;
			wr_unlock(&cgpu->qlock);

			if (unlikely(!need_work))
				discard_work(work);
		}
		/* The queue_full function should be used by the driver to
		 * actually place work items on the physical device if it
		 * does have a queue. */
	} while (!drv->queue_full(cgpu));
}

/* Add a work item to a cgpu's queued hashlist */
void __add_queued(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->queued_count++;
	HASH_ADD_INT(cgpu->queued_work, id, work);
}

struct work *__get_queued(struct cgpu_info *cgpu)
{
	struct work *work = NULL;

	if (cgpu->unqueued_work) {
		work = cgpu->unqueued_work;
		if (unlikely(stale_work(work, false))) {
			discard_work(work);
			wake_gws();
		} else
			__add_queued(cgpu, work);
		cgpu->unqueued_work = NULL;
	}

	return work;
}

/* This function is for retrieving one work item from the unqueued pointer and
 * adding it to the hashtable of queued work. Code using this function must be
 * able to handle NULL as a return which implies there is no work available. */
struct work *get_queued(struct cgpu_info *cgpu)
{
	struct work *work;

	wr_lock(&cgpu->qlock);
	work = __get_queued(cgpu);
	wr_unlock(&cgpu->qlock);

	return work;
}

void add_queued(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	__add_queued(cgpu, work);
	wr_unlock(&cgpu->qlock);
}

/* Get fresh work and add it to cgpu's queued hashlist */
struct work *get_queue_work(struct thr_info *thr, struct cgpu_info *cgpu, int thr_id)
{
	struct work *work = get_work(thr, thr_id);

	add_queued(cgpu, work);
	return work;
}

/* This function is for finding an already queued work item in the
 * given que hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The calling function must lock access to the que if it is required.
 * The common values for midstatelen, offset, datalen are 32, 64, 12 */
struct work *__find_work_bymidstate(struct work *que, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work, *tmp, *ret = NULL;

	HASH_ITER(hh, que, work, tmp) {
		if (memcmp(work->midstate, midstate, midstatelen) == 0 &&
		    memcmp(work->data + offset, data, datalen) == 0) {
			ret = work;
			break;
		}
	}

	return ret;
}

/* This function is for finding an already queued work item in the
 * device's queued_work hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The common values for midstatelen, offset, datalen are 32, 64, 12 */
struct work *find_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *ret;

	rd_lock(&cgpu->qlock);
	ret = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	rd_unlock(&cgpu->qlock);

	return ret;
}

struct work *clone_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work, *ret = NULL;

	rd_lock(&cgpu->qlock);
	work = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	if (work)
		ret = copy_work(work);
	rd_unlock(&cgpu->qlock);

	return ret;
}

/* This function is for finding an already queued work item in the
 * given que hashtable. Code using this function must be able
 * to handle NULL as a return which implies there is no matching work.
 * The calling function must lock access to the que if it is required. */
struct work *__find_work_byid(struct work *que, uint32_t id)
{
	struct work *work, *tmp, *ret = NULL;

	HASH_ITER(hh, que, work, tmp) {
		if (work->id == id) {
			ret = work;
			break;
		}
	}

	return ret;
}

struct work *find_queued_work_byid(struct cgpu_info *cgpu, uint32_t id)
{
	struct work *ret;

	rd_lock(&cgpu->qlock);
	ret = __find_work_byid(cgpu->queued_work, id);
	rd_unlock(&cgpu->qlock);

	return ret;
}

struct work *clone_queued_work_byid(struct cgpu_info *cgpu, uint32_t id)
{
	struct work *work, *ret = NULL;

	rd_lock(&cgpu->qlock);
	work = __find_work_byid(cgpu->queued_work, id);
	if (work)
		ret = copy_work(work);
	rd_unlock(&cgpu->qlock);

	return ret;
}

void __work_completed(struct cgpu_info *cgpu, struct work *work)
{
	cgpu->queued_count--;
	HASH_DEL(cgpu->queued_work, work);
}

/* This iterates over a queued hashlist finding work started more than secs
 * seconds ago and discards the work as completed. The driver must set the
 * work->tv_work_start value appropriately. Returns the number of items aged. */
int age_queued_work(struct cgpu_info *cgpu, double secs)
{
	struct work *work, *tmp;
	struct timeval tv_now;
	int aged = 0;

	cgtime(&tv_now);

	wr_lock(&cgpu->qlock);
	HASH_ITER(hh, cgpu->queued_work, work, tmp) {
		if (tdiff(&tv_now, &work->tv_work_start) > secs) {
			__work_completed(cgpu, work);
			free_work(work);
			aged++;
		}
	}
	wr_unlock(&cgpu->qlock);

	return aged;
}

/* This function should be used by queued device drivers when they're sure
 * the work struct is no longer in use. */
void work_completed(struct cgpu_info *cgpu, struct work *work)
{
	wr_lock(&cgpu->qlock);
	__work_completed(cgpu, work);
	wr_unlock(&cgpu->qlock);

	free_work(work);
}

/* Combines find_queued_work_bymidstate and work_completed in one function
 * withOUT destroying the work so the driver must free it. */
struct work *take_queued_work_bymidstate(struct cgpu_info *cgpu, char *midstate, size_t midstatelen, char *data, int offset, size_t datalen)
{
	struct work *work;

	wr_lock(&cgpu->qlock);
	work = __find_work_bymidstate(cgpu->queued_work, midstate, midstatelen, data, offset, datalen);
	if (work)
		__work_completed(cgpu, work);
	wr_unlock(&cgpu->qlock);

	return work;
}

void flush_queue(struct cgpu_info *cgpu)
{
	struct work *work = NULL;

	if (unlikely(!cgpu))
		return;

	/* Use only a trylock in case we get into a deadlock with a queueing
	 * function holding the read lock when we're called. */
	if (wr_trylock(&cgpu->qlock))
		return;
	work = cgpu->unqueued_work;
	cgpu->unqueued_work = NULL;
	wr_unlock(&cgpu->qlock);

	if (work) {
		free_work(work);
		applog(LOG_DEBUG, "Discarded queued work item");
	}
}

/* This version of hash work is for devices that are fast enough to always
 * perform a full nonce range and need a queue to maintain the device busy.
 * Work creation and destruction is not done from within this function
 * directly. */
void hash_queued_work(struct thr_info *mythr)
{
	struct timeval tv_start = {0, 0}, tv_end;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	const int thr_id = mythr->id;
	int64_t hashes_done = 0;

	while (likely(!cgpu->shutdown)) {
		struct timeval diff;
		int64_t hashes;

		mythr->work_update = false;

		fill_queue(mythr, cgpu, drv, thr_id);

		hashes = drv->scanwork(mythr);

		/* Reset the bool here in case the driver looks for it
		 * synchronously in the scanwork loop. */
		mythr->work_restart = false;

		if (unlikely(hashes == -1 )) {
			applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
			cgpu->deven = DEV_DISABLED;
			dev_error(cgpu, REASON_THREAD_ZERO_HASH);
			break;
		}

		hashes_done += hashes;
		cgtime(&tv_end);
		timersub(&tv_end, &tv_start, &diff);
		/* Update the hashmeter at most 5 times per second */
		if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
		    diff.tv_sec >= opt_log_interval) {
			hashmeter(thr_id, hashes_done);
			hashes_done = 0;
			copy_time(&tv_start, &tv_end);
		}

		if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
			mt_disable(mythr, thr_id, drv);

		if (mythr->work_update)
			drv->update_work(cgpu);
	}
	cgpu->deven = DEV_DISABLED;
}

/* This version of hash_work is for devices drivers that want to do their own
 * work management entirely, usually by using get_work(). Note that get_work
 * is a blocking function and will wait indefinitely if no work is available
 * so this must be taken into consideration in the driver. */
void hash_driver_work(struct thr_info *mythr)
{
	struct timeval tv_start = {0, 0}, tv_end;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	const int thr_id = mythr->id;
	int64_t hashes_done = 0;

	while (likely(!cgpu->shutdown)) {
		struct timeval diff;
		int64_t hashes;

		mythr->work_update = false;

		hashes = drv->scanwork(mythr);

		/* Reset the bool here in case the driver looks for it
		 * synchronously in the scanwork loop. */
		mythr->work_restart = false;

		if (unlikely(hashes == -1 )) {
			applog(LOG_ERR, "%s %d failure, disabling!", drv->name, cgpu->device_id);
			cgpu->deven = DEV_DISABLED;
			dev_error(cgpu, REASON_THREAD_ZERO_HASH);
			break;
		}

		hashes_done += hashes;
		cgtime(&tv_end);
		timersub(&tv_end, &tv_start, &diff);
		/* Update the hashmeter at most 5 times per second */
		if ((hashes_done && (diff.tv_sec > 0 || diff.tv_usec > 200000)) ||
		    diff.tv_sec >= opt_log_interval) {
			hashmeter(thr_id, hashes_done);
			hashes_done = 0;
			copy_time(&tv_start, &tv_end);
		}

		if (unlikely(mythr->pause || cgpu->deven != DEV_ENABLED))
			mt_disable(mythr, thr_id, drv);

		if (mythr->work_update)
			drv->update_work(cgpu);
	}
	cgpu->deven = DEV_DISABLED;
}

void *miner_thread(void *userdata)
{
	struct thr_info *mythr = userdata;
	const int thr_id = mythr->id;
	struct cgpu_info *cgpu = mythr->cgpu;
	struct device_drv *drv = cgpu->drv;
	char threadname[16];

        snprintf(threadname, sizeof(threadname), "%d/Miner", thr_id);
	RenameThread(threadname);

	thread_reportout(mythr);
	if (!drv->thread_init(mythr)) {
		dev_error(cgpu, REASON_THREAD_FAIL_INIT);
		goto out;
	}

	applog(LOG_DEBUG, "Waiting on sem in miner thread");
	cgsem_wait(&mythr->sem);

	cgpu->last_device_valid_work = time(NULL);
	drv->hash_work(mythr);
	drv->thread_shutdown(mythr);
out:
	return NULL;
}

enum {
	STAT_SLEEP_INTERVAL		= 1,
	STAT_CTR_INTERVAL		= 10000000,
	FAILURE_INTERVAL		= 30,
};


/* This will make the longpoll thread wait till it's the current pool, or it
 * has been flagged as rejecting, before attempting to open any connections.
 */
static void wait_lpcurrent(struct pool *pool)
{
	while (!cnx_needed(pool) && (pool->enabled == POOL_DISABLED ||
	       (pool != current_pool() && pool_strategy != POOL_LOADBALANCE &&
	       pool_strategy != POOL_BALANCE))) {
		mutex_lock(&lp_lock);
		pthread_cond_wait(&lp_cond, &lp_lock);
		mutex_unlock(&lp_lock);
	}
}

static void *longpoll_thread(void __maybe_unused *userdata)
{
	pthread_detach(pthread_self());
	return NULL;
}


void reinit_device(struct cgpu_info *cgpu)
{
	if (cgpu->deven == DEV_DISABLED)
		return;

#ifdef USE_USBUTILS
	/* Attempt a usb device reset if the device has gone sick */
	if (cgpu->usbdev && cgpu->usbdev->handle)
		libusb_reset_device(cgpu->usbdev->handle);
#endif
	cgpu->drv->reinit_device(cgpu);
}

static struct timeval rotate_tv;

/* We reap curls if they are unused for over a minute */
static void reap_curl(struct pool *pool)
{
	struct curl_ent *ent, *iter;
	struct timeval now;
	int reaped = 0;

	cgtime(&now);

	mutex_lock(&pool->pool_lock);
	list_for_each_entry_safe(ent, iter, &pool->curlring, node) {
		if (pool->curls < 2)
			break;
		if (now.tv_sec - ent->tv.tv_sec > 300) {
			reaped++;
			pool->curls--;
			list_del(&ent->node);
			curl_easy_cleanup(ent->curl);
			free(ent);
		}
	}
	mutex_unlock(&pool->pool_lock);

	if (reaped)
		applog(LOG_DEBUG, "Reaped %d curl%s from pool %d", reaped, reaped > 1 ? "s" : "", pool->pool_no);
}

/* Prune old shares we haven't had a response about for over 2 minutes in case
 * the pool never plans to respond and we're just leaking memory. If we get a
 * response beyond that time they will be seen as untracked shares. */
static void prune_stratum_shares(struct pool *pool)
{
	struct stratum_share *sshare, *tmpshare;
	time_t current_time = time(NULL);
	int cleared = 0;

	mutex_lock(&sshare_lock);
	HASH_ITER(hh, stratum_shares, sshare, tmpshare) {
		if (sshare->work->pool == pool && current_time > sshare->sshare_time + 120) {
			HASH_DEL(stratum_shares, sshare);
			free_work(sshare->work);
			free(sshare);
			cleared++;
		}
	}
	mutex_unlock(&sshare_lock);

	if (cleared) {
		applog(LOG_WARNING, "Lost %d shares due to no stratum share response from pool %d",
		       cleared, pool->pool_no);
		pool->stale_shares += cleared;
		total_stale += cleared;
	}
}

static void *watchpool_thread(void __maybe_unused *userdata)
{
	int intervals = 0;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Watchpool");

	set_lowprio();

	while (42) {
		struct timeval now;
		int i;

		if (++intervals > 20)
			intervals = 0;
		cgtime(&now);

		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			if (!opt_benchmark && !opt_benchfile) {
				reap_curl(pool);
				prune_stratum_shares(pool);
			}

			/* Get a rolling utility per pool over 10 mins */
			if (intervals > 19) {
				double shares = pool->diff1 - pool->last_shares;

				pool->last_shares = pool->diff1;
				pool->utility = (pool->utility + shares * 0.63) / 1.63;
				pool->shares = pool->utility;
			}

			if (pool->enabled == POOL_DISABLED)
				continue;

			/* Don't start testing a pool if its test thread
			 * from startup is still doing its first attempt. */
			if (unlikely(pool->testing))
				continue;

			/* Test pool is idle once every minute */
			if (pool->idle && now.tv_sec - pool->tv_idle.tv_sec > 30) {
				if (pool_active(pool, true) && pool_tclear(pool, &pool->idle))
					pool_resus(pool);
				else
					cgtime(&pool->tv_idle);
			}

			/* Only switch pools if the failback pool has been
			 * alive for more than 5 minutes to prevent
			 * intermittently failing pools from being used. */
			if (!pool->idle && pool_strategy == POOL_FAILOVER && pool->prio < cp_prio() &&
			    now.tv_sec - pool->tv_idle.tv_sec > 300) {
				applog(LOG_WARNING, "Pool %d %s stable for 5 mins",
				       pool->pool_no, pool->rpc_url);
				switch_pools(NULL);
			}
		}

		if (current_pool()->idle)
			switch_pools(NULL);

		if (pool_strategy == POOL_ROTATE && now.tv_sec - rotate_tv.tv_sec > 60 * opt_rotate_period) {
			cgtime(&rotate_tv);
			switch_pools(NULL);
		}

		cgsleep_ms(30000);
			
	}
	return NULL;
}

/* Makes sure the hashmeter keeps going even if mining threads stall, updates
 * the screen at regular intervals, and restarts threads if they appear to have
 * died. */
#define WATCHDOG_INTERVAL		2
#define WATCHDOG_SICK_TIME		120
#define WATCHDOG_DEAD_TIME		600
#define WATCHDOG_SICK_COUNT		(WATCHDOG_SICK_TIME/WATCHDOG_INTERVAL)
#define WATCHDOG_DEAD_COUNT		(WATCHDOG_DEAD_TIME/WATCHDOG_INTERVAL)

static void *watchdog_thread(void __maybe_unused *userdata)
{
	const unsigned int interval = WATCHDOG_INTERVAL;
	struct timeval zero_tv;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Watchdog");

	set_lowprio();
	memset(&zero_tv, 0, sizeof(struct timeval));
	cgtime(&rotate_tv);

	while (1) {
		int i;
		struct timeval now;

		sleep(interval);

		discard_stale();

		hashmeter(-1, 0);
		cgtime(&now);

		if (!sched_paused && !should_run()) {
			applog(LOG_WARNING, "Pausing execution as per stop time %02d:%02d scheduled",
			       schedstop.tm.tm_hour, schedstop.tm.tm_min);
			if (!schedstart.enable) {
				quit(0, "Terminating execution as planned");
				break;
			}

			applog(LOG_WARNING, "Will restart execution as scheduled at %02d:%02d",
			       schedstart.tm.tm_hour, schedstart.tm.tm_min);
			sched_paused = true;

			rd_lock(&mining_thr_lock);
			for (i = 0; i < mining_threads; i++)
				mining_thr[i]->pause = true;
			rd_unlock(&mining_thr_lock);
		} else if (sched_paused && should_run()) {
			applog(LOG_WARNING, "Restarting execution as per start time %02d:%02d scheduled",
				schedstart.tm.tm_hour, schedstart.tm.tm_min);
			if (schedstop.enable)
				applog(LOG_WARNING, "Will pause execution as scheduled at %02d:%02d",
					schedstop.tm.tm_hour, schedstop.tm.tm_min);
			sched_paused = false;

			for (i = 0; i < mining_threads; i++) {
				struct thr_info *thr;

				thr = get_thread(i);

				/* Don't touch disabled devices */
				if (thr->cgpu->deven == DEV_DISABLED)
					continue;
				thr->pause = false;
				applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
				cgsem_post(&thr->sem);
			}
		}

		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = get_devices(i);
			struct thr_info *thr = cgpu->thr[0];
			enum dev_enable *denable;
			char dev_str[8];
			int gpu;

			if (!thr)
				continue;

			cgpu->drv->get_stats(cgpu);

			gpu = cgpu->device_id;
			denable = &cgpu->deven;
			snprintf(dev_str, sizeof(dev_str), "%s%d", cgpu->drv->name, gpu);

			/* Thread is waiting on getwork or disabled */
			if (thr->getwork || *denable == DEV_DISABLED)
				continue;

			if (cgpu->status != LIFE_WELL && (now.tv_sec - thr->last.tv_sec < WATCHDOG_SICK_TIME)) {
				if (cgpu->status != LIFE_INIT)
				applog(LOG_ERR, "%s: Recovered, declaring WELL!", dev_str);
				cgpu->status = LIFE_WELL;
				cgpu->device_last_well = time(NULL);
			} else if (cgpu->status == LIFE_WELL && (now.tv_sec - thr->last.tv_sec > WATCHDOG_SICK_TIME)) {
				cgpu->rolling = 0;
				cgpu->status = LIFE_SICK;
				applog(LOG_ERR, "%s: Idle for more than 60 seconds, declaring SICK!", dev_str);
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_SICK_IDLE_60);
				if (opt_restart) {
					applog(LOG_ERR, "%s: Attempting to restart", dev_str);
					reinit_device(cgpu);
				}
			} else if (cgpu->status == LIFE_SICK && (now.tv_sec - thr->last.tv_sec > WATCHDOG_DEAD_TIME)) {
				cgpu->status = LIFE_DEAD;
				applog(LOG_ERR, "%s: Not responded for more than 10 minutes, declaring DEAD!", dev_str);
				cgtime(&thr->sick);

				dev_error(cgpu, REASON_DEV_DEAD_IDLE_600);
			} else if (now.tv_sec - thr->sick.tv_sec > 60 &&
				   (cgpu->status == LIFE_SICK || cgpu->status == LIFE_DEAD)) {
				/* Attempt to restart a GPU that's sick or dead once every minute */
				cgtime(&thr->sick);
				if (opt_restart)
					reinit_device(cgpu);
			}
		}
	}

	return NULL;
}

static void log_print_status(struct cgpu_info *cgpu)
{
	char logline[255];

	get_statline(logline, sizeof(logline), cgpu);
	applog(LOG_WARNING, "%s", logline);
}

static void noop_get_statline(char __maybe_unused *buf, size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu);
void blank_get_statline_before(char *buf, size_t bufsiz, struct cgpu_info __maybe_unused *cgpu);

void print_summary(void)
{
	struct timeval diff;
	int hours, mins, secs, i;
	double utility, displayed_hashes, work_util;

	timersub(&total_tv_end, &total_tv_start, &diff);
	hours = diff.tv_sec / 3600;
	mins = (diff.tv_sec % 3600) / 60;
	secs = diff.tv_sec % 60;

	utility = total_accepted / total_secs * 60;
	work_util = total_diff1 / total_secs * 60;

	applog(LOG_WARNING, "\nSummary of runtime statistics:\n");
	applog(LOG_WARNING, "Started at %s", datestamp);
	if (total_pools == 1)
		applog(LOG_WARNING, "Pool: %s", pools[0]->rpc_url);
	applog(LOG_WARNING, "Runtime: %d hrs : %d mins : %d secs", hours, mins, secs);
	displayed_hashes = total_mhashes_done / total_secs;

	applog(LOG_WARNING, "Average hashrate: %.1f Mhash/s", displayed_hashes);
	applog(LOG_WARNING, "Solved blocks: %d", found_blocks);
	applog(LOG_WARNING, "Best share difficulty: %s", best_share);
	applog(LOG_WARNING, "Share submissions: %"PRId64, total_accepted + total_rejected);
	applog(LOG_WARNING, "Accepted shares: %"PRId64, total_accepted);
	applog(LOG_WARNING, "Rejected shares: %"PRId64, total_rejected);
	applog(LOG_WARNING, "Accepted difficulty shares: %1.f", total_diff_accepted);
	applog(LOG_WARNING, "Rejected difficulty shares: %1.f", total_diff_rejected);
	if (total_accepted || total_rejected)
		applog(LOG_WARNING, "Reject ratio: %.1f%%", (double)(total_rejected * 100) / (double)(total_accepted + total_rejected));
	applog(LOG_WARNING, "Hardware errors: %d", hw_errors);
	applog(LOG_WARNING, "Utility (accepted shares / min): %.2f/min", utility);
	applog(LOG_WARNING, "Work Utility (diff1 shares solved / min): %.2f/min\n", work_util);

	applog(LOG_WARNING, "Stale submissions discarded due to new blocks: %"PRId64, total_stale);
	applog(LOG_WARNING, "Unable to get work from server occasions: %d", total_go);
	applog(LOG_WARNING, "Work items generated locally: %d", local_work);
	applog(LOG_WARNING, "Submitting work remotely delay occasions: %d", total_ro);
	applog(LOG_WARNING, "New blocks detected on network: %d\n", new_blocks);

	if (total_pools > 1) {
		for (i = 0; i < total_pools; i++) {
			struct pool *pool = pools[i];

			applog(LOG_WARNING, "Pool: %s", pool->rpc_url);
			if (pool->solved)
				applog(LOG_WARNING, "SOLVED %d BLOCK%s!", pool->solved, pool->solved > 1 ? "S" : "");
			applog(LOG_WARNING, " Share submissions: %"PRId64, pool->accepted + pool->rejected);
			applog(LOG_WARNING, " Accepted shares: %"PRId64, pool->accepted);
			applog(LOG_WARNING, " Rejected shares: %"PRId64, pool->rejected);
			applog(LOG_WARNING, " Accepted difficulty shares: %1.f", pool->diff_accepted);
			applog(LOG_WARNING, " Rejected difficulty shares: %1.f", pool->diff_rejected);
			if (pool->accepted || pool->rejected)
				applog(LOG_WARNING, " Reject ratio: %.1f%%", (double)(pool->rejected * 100) / (double)(pool->accepted + pool->rejected));

			applog(LOG_WARNING, " Items worked on: %d", pool->works);
			applog(LOG_WARNING, " Stale submissions discarded due to new blocks: %d", pool->stale_shares);
			applog(LOG_WARNING, " Unable to get work from server occasions: %d", pool->getfail_occasions);
			applog(LOG_WARNING, " Submitting work remotely delay occasions: %d\n", pool->remotefail_occasions);
		}
	}

	applog(LOG_WARNING, "Summary of per device statistics:\n");
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = get_devices(i);

		cgpu->drv->get_statline_before = &blank_get_statline_before;
		cgpu->drv->get_statline = &noop_get_statline;
		log_print_status(cgpu);
	}

	if (opt_shares) {
		applog(LOG_WARNING, "Mined %.0f accepted shares of %d requested\n", total_diff_accepted, opt_shares);
		if (opt_shares > total_diff_accepted)
			applog(LOG_WARNING, "WARNING - Mined only %.0f shares of %d requested.", total_diff_accepted, opt_shares);
	}
	applog(LOG_WARNING, " ");

	fflush(stderr);
	fflush(stdout);
}

static void clean_up(bool restarting)
{
#ifdef USE_USBUTILS
	usb_polling = false;
	pthread_join(usb_poll_thread, NULL);
        libusb_exit(NULL);
#endif

	cgtime(&total_tv_end);
	if (!restarting && !opt_realquiet && successful_connect)
		print_summary();

	curl_global_cleanup();
}

/* Should all else fail and we're unable to clean up threads due to locking
 * issues etc, just silently exit. */
static void *killall_thread(void __maybe_unused *arg)
{
	pthread_detach(pthread_self());
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	sleep(5);
	exit(1);
	return NULL;
}

void __quit(int status, bool clean)
{
	pthread_t killall_t;

	if (unlikely(pthread_create(&killall_t, NULL, killall_thread, NULL)))
		exit(1);

	if (clean)
		clean_up(false);

	if (forkpid > 0) {
		kill(forkpid, SIGTERM);
		forkpid = 0;
	}
	pthread_cancel(killall_t);
	exit(status);
}

void _quit(int status)
{
	__quit(status, true);
}


static bool pools_active = false;

static void *test_pool_thread(void *arg)
{
	struct pool *pool = (struct pool *)arg;

	if (!pool->blocking)
		pthread_detach(pthread_self());
retry:
	if (pool_active(pool, false)) {
		pool_tset(pool, &pool->lagging);
		pool_tclear(pool, &pool->idle);
		bool first_pool = false;

		cg_wlock(&control_lock);
		if (!pools_active) {
			currentpool = pool;
			if (pool->pool_no != 0)
				first_pool = true;
			pools_active = true;
		}
		cg_wunlock(&control_lock);

		if (unlikely(first_pool))
			applog(LOG_NOTICE, "Switching to pool %d %s - first alive pool", pool->pool_no, pool->rpc_url);

		pool_resus(pool);
		switch_pools(NULL);
	} else {
		pool_died(pool);
		sleep(5);
		goto retry;
	}

	pool->testing = false;

	return NULL;
}

/* Always returns true that the pool details were added unless we are not
 * live, implying this is the only pool being added, so if no pools are
 * active it returns false. */
bool add_pool_details(struct pool *pool, bool live, char *url, char *user, char *pass)
{
	size_t siz;

	url = get_proxy(url, pool);

	pool->rpc_url = url;
	pool->rpc_user = user;
	pool->rpc_pass = pass;
	siz = strlen(pool->rpc_user) + strlen(pool->rpc_pass) + 2;
	pool->rpc_userpass = malloc(siz);
	if (!pool->rpc_userpass)
		quit(1, "Failed to malloc userpass");
	snprintf(pool->rpc_userpass, siz, "%s:%s", pool->rpc_user, pool->rpc_pass);

	pool->testing = true;
	pool->idle = true;
	pool->blocking = !live;
	enable_pool(pool);

	pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	if (!live) {
		pthread_join(pool->test_thread, NULL);
		return pools_active;
	}
	return true;
}



static int cgminer_id_count = 0;

/* Various noop functions for drivers that don't support or need their
 * variants. */
static void noop_reinit_device(struct cgpu_info __maybe_unused *cgpu)
{
}

void blank_get_statline_before(char __maybe_unused *buf,size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu)
{
}

static void noop_get_statline(char __maybe_unused *buf, size_t __maybe_unused bufsiz, struct cgpu_info __maybe_unused *cgpu)
{
}

static bool noop_get_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return true;
}

static bool noop_thread_prepare(struct thr_info __maybe_unused *thr)
{
	return true;
}

static uint64_t noop_can_limit_work(struct thr_info __maybe_unused *thr)
{
	return 0xffffffff;
}

static bool noop_thread_init(struct thr_info __maybe_unused *thr)
{
	return true;
}

static bool noop_prepare_work(struct thr_info __maybe_unused *thr, struct work __maybe_unused *work)
{
	return true;
}

static void noop_hw_error(struct thr_info __maybe_unused *thr)
{
}

static void noop_thread_shutdown(struct thr_info __maybe_unused *thr)
{
}

static void noop_thread_enable(struct thr_info __maybe_unused *thr)
{
}

static void noop_detect(bool __maybe_unused hotplug)
{
}

static struct api_data *noop_get_api_stats(struct cgpu_info __maybe_unused *cgpu)
{
	return NULL;
}

static void noop_hash_work(struct thr_info __maybe_unused *thr)
{
}

#define noop_flush_work noop_reinit_device
#define noop_update_work noop_reinit_device
#define noop_queue_full noop_get_stats
#define noop_zero_stats noop_reinit_device
#define noop_identify_device noop_reinit_device

/* Fill missing driver drv functions with noops */
void fill_device_drv(struct device_drv *drv)
{
	if (!drv->drv_detect)
		drv->drv_detect = &noop_detect;
	if (!drv->reinit_device)
		drv->reinit_device = &noop_reinit_device;
	if (!drv->get_statline_before)
		drv->get_statline_before = &blank_get_statline_before;
	if (!drv->get_statline)
		drv->get_statline = &noop_get_statline;
	if (!drv->get_stats)
		drv->get_stats = &noop_get_stats;
	if (!drv->thread_prepare)
		drv->thread_prepare = &noop_thread_prepare;
	if (!drv->can_limit_work)
		drv->can_limit_work = &noop_can_limit_work;
	if (!drv->thread_init)
		drv->thread_init = &noop_thread_init;
	if (!drv->prepare_work)
		drv->prepare_work = &noop_prepare_work;
	if (!drv->hw_error)
		drv->hw_error = &noop_hw_error;
	if (!drv->thread_shutdown)
		drv->thread_shutdown = &noop_thread_shutdown;
	if (!drv->thread_enable)
		drv->thread_enable = &noop_thread_enable;
	if (!drv->hash_work)
		drv->hash_work = &hash_sole_work;
	if (!drv->flush_work)
		drv->flush_work = &noop_flush_work;
	if (!drv->update_work)
		drv->update_work = &noop_update_work;
	if (!drv->queue_full)
		drv->queue_full = &noop_queue_full;
	if (!drv->zero_stats)
		drv->zero_stats = &noop_zero_stats;
	/* If drivers support internal diff they should set a max_diff or
	 * we will assume they don't and set max to 1. */
	if (!drv->max_diff)
		drv->max_diff = 1;
}

void null_device_drv(struct device_drv *drv)
{
	drv->drv_detect = &noop_detect;
	drv->reinit_device = &noop_reinit_device;
	drv->get_statline_before = &blank_get_statline_before;
	drv->get_statline = &noop_get_statline;
	drv->get_api_stats = &noop_get_api_stats;
	drv->get_stats = &noop_get_stats;
	drv->identify_device = &noop_identify_device;
	drv->set_device = NULL;

	drv->thread_prepare = &noop_thread_prepare;
	drv->can_limit_work = &noop_can_limit_work;
	drv->thread_init = &noop_thread_init;
	drv->prepare_work = &noop_prepare_work;

	/* This should make the miner thread just exit */
	drv->hash_work = &noop_hash_work;

	drv->hw_error = &noop_hw_error;
	drv->thread_shutdown = &noop_thread_shutdown;
	drv->thread_enable = &noop_thread_enable;

	drv->zero_stats = &noop_zero_stats;

	drv->hash_work = &noop_hash_work;

	drv->queue_full = &noop_queue_full;
	drv->flush_work = &noop_flush_work;
	drv->update_work = &noop_update_work;

	drv->zero_stats = &noop_zero_stats;
	drv->max_diff = 1;
	drv->min_diff = 1;
}

void enable_device(struct cgpu_info *cgpu)
{
	cgpu->deven = DEV_ENABLED;

	wr_lock(&devices_lock);
	devices[cgpu->cgminer_id = cgminer_id_count++] = cgpu;
	wr_unlock(&devices_lock);

	if (hotplug_mode)
		new_threads += cgpu->threads;
	else
		mining_threads += cgpu->threads;

	rwlock_init(&cgpu->qlock);
	cgpu->queued_work = NULL;
}

struct _cgpu_devid_counter {
	char name[4];
	int lastid;
	UT_hash_handle hh;
};

static void adjust_mostdevs(void)
{
	if (total_devices - zombie_devs > most_devices)
		most_devices = total_devices - zombie_devs;
}


bool add_cgpu(struct cgpu_info *cgpu)
{
	static struct _cgpu_devid_counter *devids = NULL;
	struct _cgpu_devid_counter *d;
	
	HASH_FIND_STR(devids, cgpu->drv->name, d);
	if (d)
		cgpu->device_id = ++d->lastid;
	else {
		d = malloc(sizeof(*d));
		memcpy(d->name, cgpu->drv->name, sizeof(d->name));
		cgpu->device_id = d->lastid = 0;
		HASH_ADD_STR(devids, name, d);
	}

	wr_lock(&devices_lock);
	devices = realloc(devices, sizeof(struct cgpu_info *) * (total_devices + new_devices + 2));
	wr_unlock(&devices_lock);

	mutex_lock(&stats_lock);
	cgpu->last_device_valid_work = time(NULL);
	mutex_unlock(&stats_lock);

	if (hotplug_mode)
		devices[total_devices + new_devices++] = cgpu;
	else
		devices[total_devices++] = cgpu;

	adjust_mostdevs();
#ifdef USE_USBUTILS
	if (cgpu->usbdev && !cgpu->unique_id && cgpu->usbdev->serial_string &&
	    strlen(cgpu->usbdev->serial_string) > 4)
		cgpu->unique_id = str_text(cgpu->usbdev->serial_string);
#endif
	return true;
}

struct device_drv *copy_drv(struct device_drv *drv)
{
	struct device_drv *copy;

	if (unlikely(!(copy = malloc(sizeof(*copy))))) {
		quit(1, "Failed to allocate device_drv copy of %s (%s)",
				drv->name, drv->copy ? "copy" : "original");
	}
	memcpy(copy, drv, sizeof(*copy));
	copy->copy = true;
	return copy;
}

#ifdef USE_USBUTILS
static void hotplug_process(void)
{
	struct thr_info *thr;
	int i, j;

	for (i = 0; i < new_devices; i++) {
		struct cgpu_info *cgpu;
		int dev_no = total_devices + i;

		cgpu = devices[dev_no];
		enable_device(cgpu);
		cgpu->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;
		cgpu->rolling = cgpu->total_mhashes = 0;
	}

	wr_lock(&mining_thr_lock);
	mining_thr = realloc(mining_thr, sizeof(thr) * (mining_threads + new_threads + 1));

	if (!mining_thr)
		quit(1, "Failed to hotplug realloc mining_thr");
	for (i = 0; i < new_threads; i++) {
		mining_thr[mining_threads + i] = calloc(1, sizeof(*thr));
		if (!mining_thr[mining_threads + i])
			quit(1, "Failed to hotplug calloc mining_thr[%d]", i);
	}

	// Start threads
	for (i = 0; i < new_devices; ++i) {
		struct cgpu_info *cgpu = devices[total_devices];
		cgpu->thr = malloc(sizeof(*cgpu->thr) * (cgpu->threads+1));
		cgpu->thr[cgpu->threads] = NULL;
		cgpu->status = LIFE_INIT;
		cgtime(&(cgpu->dev_start_tv));

		for (j = 0; j < cgpu->threads; ++j) {
			thr = __get_thread(mining_threads);
			thr->id = mining_threads;
			thr->cgpu = cgpu;
			thr->device_thread = j;

			if (!cgpu->drv->thread_prepare(thr)) {
				null_device_drv(cgpu->drv);
				cgpu->deven = DEV_DISABLED;
				continue;
			}

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				quit(1, "hotplug thread %d create failed", thr->id);

			cgpu->thr[j] = thr;

			/* Enable threads for devices set not to mine but disable
			 * their queue in case we wish to enable them later */
			if (cgpu->deven != DEV_DISABLED) {
				applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
				cgsem_post(&thr->sem);
			}

			mining_threads++;
		}
		total_devices++;
		applog(LOG_WARNING, "Hotplug: %s added %s %i", cgpu->drv->dname, cgpu->drv->name, cgpu->device_id);
	}
	wr_unlock(&mining_thr_lock);

	adjust_mostdevs();
}

#define DRIVER_DRV_DETECT_HOTPLUG(X) X##_drv.drv_detect(true);

static void *hotplug_thread(void __maybe_unused *userdata)
{
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	RenameThread("Hotplug");

	hotplug_mode = true;

	cgsleep_ms(5000);

	while (0x2a) {
// Version 0.1 just add the devices on - worry about using nodev later

		if (hotplug_time == 0)
			cgsleep_ms(5000);
		else {
			new_devices = 0;
			new_threads = 0;

			/* Use the DRIVER_PARSE_COMMANDS macro to detect all
			 * devices */
			DRIVER_PARSE_COMMANDS(DRIVER_DRV_DETECT_HOTPLUG)

			if (new_devices)
				hotplug_process();

			// hotplug_time >0 && <=9999
			cgsleep_ms(hotplug_time * 1000);
		}
	}

	return NULL;
}
#endif

static void probe_pools(void)
{
	int i;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool = pools[i];

		pool->testing = true;
		pthread_create(&pool->test_thread, NULL, test_pool_thread, (void *)pool);
	}
}

#define DRIVER_FILL_DEVICE_DRV(X) fill_device_drv(&X##_drv);
#define DRIVER_DRV_DETECT_ALL(X) X##_drv.drv_detect(false);

#ifdef USE_USBUTILS
static void *libusb_poll_thread(void __maybe_unused *arg)
{
	struct timeval tv_end = {1, 0};

	RenameThread("USBPoll");

	while (usb_polling)
		libusb_handle_events_timeout_completed(NULL, &tv_end, NULL);

	/* Cancel any cancellable usb transfers */
	cancel_usb_transfers();

	/* Keep event handling going until there are no async transfers in
	 * flight. */
	do {
		libusb_handle_events_timeout_completed(NULL, &tv_end, NULL);
	} while (async_usb_transfers());

	return NULL;
}

static void initialise_usb(void) {
	int err = libusb_init(NULL);

	if (err) {
		fprintf(stderr, "libusb_init() failed err %d", err);
		fflush(stderr);
		quit(1, "libusb_init() failed");
	}
	initialise_usblocks();
	usb_polling = true;
	pthread_create(&usb_poll_thread, NULL, libusb_poll_thread, NULL);
}
#else
#define initialise_usb() {}
#endif

int main(int argc, char *argv[])
{
	struct sigaction handler;
	struct work *work = NULL;
	bool pool_msg = false;
	struct thr_info *thr;
	struct block *block;
	int i, j, slept = 0;
	unsigned int k;
	char *s;

	g_logfile_enable = false;
	strcpy(g_logfile_path, "cgminer.log");
	strcpy(g_logfile_openflag, "a+");
	

	/* If we're on a small lowspec platform with only one CPU, we should
	 * yield after dropping a lock to allow a thread waiting for it to be
	 * able to get CPU time to grab the lock. */
	if (sysconf(_SC_NPROCESSORS_ONLN) == 1)
		selective_yield = &sched_yield;

#if LOCK_TRACKING
	// Must be first
	if (unlikely(pthread_mutex_init(&lockstat_lock, NULL)))
		quithere(1, "Failed to pthread_mutex_init lockstat_lock errno=%d", errno);
#endif

	initial_args = malloc(sizeof(char *) * (argc + 1));
	for  (i = 0; i < argc; i++)
		initial_args[i] = strdup(argv[i]);
	initial_args[argc] = NULL;

	mutex_init(&hash_lock);
	mutex_init(&console_lock);
	cglock_init(&control_lock);
	mutex_init(&stats_lock);
	mutex_init(&sharelog_lock);
	cglock_init(&ch_lock);
	mutex_init(&sshare_lock);
	rwlock_init(&blk_lock);
	rwlock_init(&netacc_lock);
	rwlock_init(&mining_thr_lock);
	rwlock_init(&devices_lock);

	mutex_init(&lp_lock);
	if (unlikely(pthread_cond_init(&lp_cond, NULL)))
		early_quit(1, "Failed to pthread_cond_init lp_cond");

	mutex_init(&restart_lock);
	if (unlikely(pthread_cond_init(&restart_cond, NULL)))
		early_quit(1, "Failed to pthread_cond_init restart_cond");

	if (unlikely(pthread_cond_init(&gws_cond, NULL)))
		early_quit(1, "Failed to pthread_cond_init gws_cond");

	/* Create a unique get work queue */
	getq = tq_new();
	if (!getq)
		early_quit(1, "Failed to create getq");
	/* We use the getq mutex as the staged lock */
	stgd_lock = &getq->mutex;

	initialise_usb();

	snprintf(packagename, sizeof(packagename), "%s %s", PACKAGE, VERSION);

	handler.sa_handler = &sighandler;
	handler.sa_flags = 0;
	sigemptyset(&handler.sa_mask);
	sigaction(SIGTERM, &handler, &termhandler);
	sigaction(SIGINT, &handler, &inthandler);
	signal(SIGPIPE, SIG_IGN);

	opt_kernel_path = alloca(PATH_MAX);
	strcpy(opt_kernel_path, CGMINER_PREFIX);
	cgminer_path = alloca(PATH_MAX);
	s = strdup(argv[0]);
	strcpy(cgminer_path, dirname(s));
	free(s);
	strcat(cgminer_path, "/");

	devcursor = 8;
	logstart = devcursor + 1;
	logcursor = logstart + 1;

	block = calloc(sizeof(struct block), 1);
	if (unlikely(!block))
		quit (1, "main OOM");
	for (i = 0; i < 36; i++)
		strcat(block->hash, "0");
	HASH_ADD_STR(blocks, hash, block);
	strcpy(current_hash, block->hash);

	INIT_LIST_HEAD(&scan_devices);

	/* parse command line */
	opt_register_table(opt_config_table,
			   "Options for both config file and command line");
	opt_register_table(opt_cmdline_table,
			   "Options for command line only");

	opt_parse(&argc, argv, applog_and_exit);
	if (argc != 1)
		early_quit(1, "Unexpected extra commandline arguments");

	if (!config_loaded)
		load_default_config();

	if (opt_benchmark || opt_benchfile) {
		struct pool *pool;

		pool = add_pool();
		pool->rpc_url = malloc(255);
		if (opt_benchfile)
			strcpy(pool->rpc_url, "Benchfile");
		else
			strcpy(pool->rpc_url, "Benchmark");
		pool->rpc_user = pool->rpc_url;
		pool->rpc_pass = pool->rpc_url;
		pool->rpc_userpass = pool->rpc_url;
		pool->sockaddr_url = pool->rpc_url;
		strncpy(pool->diff, "?", sizeof(pool->diff)-1);
		pool->diff[sizeof(pool->diff)-1] = '\0';
		enable_pool(pool);
		pool->idle = false;
		successful_connect = true;

		for (i = 0; i < 16; i++) {
			hex2bin(&bench_hidiff_bins[i][0], &bench_hidiffs[i][0], 160);
			hex2bin(&bench_lodiff_bins[i][0], &bench_lodiffs[i][0], 160);
		}
		set_target(bench_target, 32);
	}
	
	if(opt_version_path) {
		FILE * fpversion = fopen(opt_version_path, "rb");
		char tmp[256] = {0};
		int len = 0;
		char * start = 0;
		if(fpversion == NULL) {
			applog(LOG_ERR, "Open miner version file %s error", opt_version_path);
		} else {
			len = fread(tmp, 1, 256, fpversion);
			if(len <= 0) {
				applog(LOG_ERR, "Read miner version file %s error %d", opt_version_path, len);
			} else {
				start = strstr(tmp, "\n");
				if(start == NULL) {
					strcpy(g_miner_compiletime, tmp);
				} else {
					memcpy(g_miner_compiletime, tmp, start-tmp);
					strcpy(g_miner_type, start+1);
				}
				if(g_miner_compiletime[strlen(g_miner_compiletime)-1] == '\n')
					g_miner_compiletime[strlen(g_miner_compiletime)-1] = 0;
				if(g_miner_compiletime[strlen(g_miner_compiletime)-1] == '\r')
					g_miner_compiletime[strlen(g_miner_compiletime)-1] = 0;
				if(g_miner_type[strlen(g_miner_type)-1] == '\n')
					g_miner_type[strlen(g_miner_type)-1] = 0;
				if(g_miner_type[strlen(g_miner_type)-1] == '\r')
					g_miner_type[strlen(g_miner_type)-1] = 0;
			}
		}
		applog(LOG_ERR, "Miner compile time: %s type: %s", g_miner_compiletime, g_miner_type);
	}

	if(opt_logfile_path) {
		g_logfile_enable = true;
		strcpy(g_logfile_path, opt_logfile_path);
		if(opt_logfile_openflag) {
			strcpy(g_logfile_openflag, opt_logfile_openflag);
		}
		applog(LOG_ERR, "Log file path: %s Open flag: %s", g_logfile_path, g_logfile_openflag);
	}

	if(opt_logwork_path) {
		char szfilepath[256] = {0};
		if(opt_logwork_asicnum) {
			if(strlen(opt_logwork_asicnum) <= 0) {
				quit(1, "Log work asic num empty");
			}
			g_logwork_asicnum = atoi(opt_logwork_asicnum);
			if(g_logwork_asicnum != 1 && g_logwork_asicnum != 32 && g_logwork_asicnum != 64) {
				quit(1, "Log work asic num must be 1, 32, 64");
			}
			applog(LOG_ERR, "Log work path: %s Asic num: %s", opt_logwork_path, opt_logwork_asicnum);
		} else {
			applog(LOG_ERR, "Log work path: %s", opt_logwork_path);
		}

		sprintf(szfilepath, "%s.txt", opt_logwork_path);
		g_logwork_file = fopen(szfilepath, "a+");
		applog(LOG_ERR, "Log work open file %s", szfilepath);

		if(g_logwork_asicnum == 1) {
			sprintf(szfilepath, "%s%02d.txt", opt_logwork_path, g_logwork_asicnum);
			g_logwork_files[0] = fopen(szfilepath, "a+");
			applog(LOG_ERR, "Log work open asic %d file %s", g_logwork_asicnum, szfilepath);
		} else if(g_logwork_asicnum == 32 || g_logwork_asicnum == 64) {
			for(i = 0; i <= g_logwork_asicnum; i++) {
				sprintf(szfilepath, "%s%02d_%02d.txt", opt_logwork_path, g_logwork_asicnum, i);
				g_logwork_files[i] = fopen(szfilepath, "a+");
				applog(LOG_ERR, "Log work open asic %d file %s", g_logwork_asicnum, szfilepath);
			}
		}

		if(opt_logwork_diff) {
			for(i = 0; i <= 64; i++) {
				sprintf(szfilepath, "%s_diff_%02d.txt", opt_logwork_path, i);
				g_logwork_diffs[i] = fopen(szfilepath, "a+");
				applog(LOG_ERR, "Log work open diff file %s", szfilepath);
			}
		}
	}

	applog(LOG_WARNING, "Started %s", packagename);
	if (cnfbuf) {
		applog(LOG_NOTICE, "Loaded configuration file %s", cnfbuf);
		switch (fileconf_load) {
			case 0:
				applog(LOG_WARNING, "Fatal JSON error in configuration file.");
				applog(LOG_WARNING, "Configuration file could not be used.");
				break;
			case -1:
				applog(LOG_WARNING, "Error in configuration file, partially loaded.");
				if (use_curses)
					applog(LOG_WARNING, "Start cgminer with -T to see what failed to load.");
				break;
			default:
				break;
		}
		free(cnfbuf);
		cnfbuf = NULL;
	}

	strcat(opt_kernel_path, "/");

	if (want_per_device_stats)
		opt_log_output = true;

	if (opt_scantime < 0)
		opt_scantime = 60;

	total_control_threads = 8;
	control_thr = calloc(total_control_threads, sizeof(*thr));
	if (!control_thr)
		early_quit(1, "Failed to calloc control_thr");

	gwsched_thr_id = 0;

#ifdef USE_USBUTILS
	usb_initialise();

	// before device detection
	cgsem_init(&usb_resource_sem);
	usbres_thr_id = 1;
	thr = &control_thr[usbres_thr_id];
	if (thr_info_create(thr, NULL, usb_resource_thread, thr))
		early_quit(1, "usb resource thread create failed");
	pthread_detach(thr->pth);
#endif

	/* Use the DRIVER_PARSE_COMMANDS macro to fill all the device_drvs */
	DRIVER_PARSE_COMMANDS(DRIVER_FILL_DEVICE_DRV)

	/* Use the DRIVER_PARSE_COMMANDS macro to detect all devices */
	DRIVER_PARSE_COMMANDS(DRIVER_DRV_DETECT_ALL)

	if (opt_display_devs) {
		applog(LOG_ERR, "Devices detected:");
		for (i = 0; i < total_devices; ++i) {
			struct cgpu_info *cgpu = devices[i];
			if (cgpu->name)
				applog(LOG_ERR, " %2d. %s %d: %s (driver: %s)", i, cgpu->drv->name, cgpu->device_id, cgpu->name, cgpu->drv->dname);
			else
				applog(LOG_ERR, " %2d. %s %d (driver: %s)", i, cgpu->drv->name, cgpu->device_id, cgpu->drv->dname);
		}
		early_quit(0, "%d devices listed", total_devices);
	}

	mining_threads = 0;
	for (i = 0; i < total_devices; ++i)
		enable_device(devices[i]);

	if (!total_devices)
		early_quit(1, "All devices disabled, cannot mine!");

	most_devices = total_devices;

	load_temp_cutoffs();

	for (i = 0; i < total_devices; ++i)
		devices[i]->cgminer_stats.getwork_wait_min.tv_sec = MIN_SEC_UNSET;

	if (!opt_compact) {
		logstart += most_devices;
		logcursor = logstart + 1;
	}

	// currentpool = pools[0];

	mining_thr = calloc(mining_threads, sizeof(thr));
	if (!mining_thr)
		early_quit(1, "Failed to calloc mining_thr");
	for (i = 0; i < mining_threads; i++) {
		mining_thr[i] = calloc(1, sizeof(*thr));
		if (!mining_thr[i])
			early_quit(1, "Failed to calloc mining_thr[%d]", i);
	}

	// Start threads
	k = 0;
	for (i = 0; i < total_devices; ++i) {
		struct cgpu_info *cgpu = devices[i];
		cgpu->thr = malloc(sizeof(*cgpu->thr) * (cgpu->threads+1));
		cgpu->thr[cgpu->threads] = NULL;
		cgpu->status = LIFE_INIT;

		for (j = 0; j < cgpu->threads; ++j, ++k) {
			thr = get_thread(k);
			thr->id = k;
			thr->cgpu = cgpu;
			thr->device_thread = j;

			if (!cgpu->drv->thread_prepare(thr))
				continue;

			if (unlikely(thr_info_create(thr, NULL, miner_thread, thr)))
				early_quit(1, "thread %d create failed", thr->id);

			cgpu->thr[j] = thr;

			/* Enable threads for devices set not to mine but disable
			 * their queue in case we wish to enable them later */
			if (cgpu->deven != DEV_DISABLED) {
				applog(LOG_DEBUG, "Pushing sem post to thread %d", thr->id);
				cgsem_post(&thr->sem);
			}
		}
	}

	if (opt_benchmark || opt_benchfile)
		goto begin_bench;

	for (i = 0; i < total_pools; i++) {
		struct pool *pool  = pools[i];

		enable_pool(pool);
		pool->idle = true;
	}

	/* Look for at least one active pool before starting */
	applog(LOG_NOTICE, "Probing for an alive pool");
	probe_pools();
	do {
		sleep(1);
		slept++;
	} while (!pools_active && slept < 60);

	while (!pools_active) {
		if (!pool_msg) {
			applog(LOG_ERR, "No servers were found that could be used to get work from.");
			applog(LOG_ERR, "Please check the details from the list below of the servers you have input");
			applog(LOG_ERR, "Most likely you have input the wrong URL, forgotten to add a port, or have not set up workers");
			for (i = 0; i < total_pools; i++) {
				struct pool *pool = pools[i];

				applog(LOG_WARNING, "Pool: %d  URL: %s  User: %s  Password: %s",
				i, pool->rpc_url, pool->rpc_user, pool->rpc_pass);
			}
			pool_msg = true;
			if (use_curses)
				applog(LOG_ERR, "Press any key to exit, or cgminer will wait indefinitely for an alive pool.");
		}
		if (!use_curses)
			early_quit(0, "No servers could be used! Exiting.");
	};

begin_bench:
	total_mhashes_done = 0;
	for(i = 0; i < CG_LOCAL_MHASHES_MAX_NUM; i++) {
		g_local_mhashes_dones[i] = 0;
	}
	g_local_mhashes_index = 0;
	for (i = 0; i < total_devices; i++) {
		struct cgpu_info *cgpu = devices[i];

		cgpu->rolling = cgpu->total_mhashes = 0;
	}
	
	cgtime(&total_tv_start);
	cgtime(&total_tv_end);
	cgtime(&tv_hashmeter);
	get_datestamp(datestamp, sizeof(datestamp), &total_tv_start);

	watchpool_thr_id = 2;
	thr = &control_thr[watchpool_thr_id];
	/* start watchpool thread */
	if (thr_info_create(thr, NULL, watchpool_thread, NULL))
		early_quit(1, "watchpool thread create failed");
	pthread_detach(thr->pth);

	watchdog_thr_id = 3;
	thr = &control_thr[watchdog_thr_id];
	/* start watchdog thread */
	if (thr_info_create(thr, NULL, watchdog_thread, NULL))
		early_quit(1, "watchdog thread create failed");
	pthread_detach(thr->pth);

	/* Create API socket thread */
	api_thr_id = 5;
	thr = &control_thr[api_thr_id];
	if (thr_info_create(thr, NULL, api_thread, thr))
		early_quit(1, "API thread create failed");

#ifdef USE_USBUTILS
	hotplug_thr_id = 6;
	thr = &control_thr[hotplug_thr_id];
	if (thr_info_create(thr, NULL, hotplug_thread, thr))
		early_quit(1, "hotplug thread create failed");
	pthread_detach(thr->pth);
#endif


	/* Just to be sure */
	if (total_control_threads != 8)
		early_quit(1, "incorrect total_control_threads (%d) should be 8", total_control_threads);

	set_highprio();

	/* Once everything is set up, main() becomes the getwork scheduler */
	while (42) {
		int ts, max_staged = max_queue;
		struct pool *pool, *cp;
		bool lagging = false;

		if (opt_work_update)
			signal_work_update();
		opt_work_update = false;
		cp = current_pool();

		/* If the primary pool is a getwork pool and cannot roll work,
		 * try to stage one extra work per mining thread */
		if (!pool_localgen(cp) && !staged_rollable)
			max_staged += mining_threads;

		mutex_lock(stgd_lock);
		ts = __total_staged();

		if (!pool_localgen(cp) && !ts && !opt_fail_only)
			lagging = true;

		/* Wait until hash_pop tells us we need to create more work */
		if (ts > max_staged) {
			if (work_emptied && max_queue < opt_queue) {
				max_queue++;
				work_emptied = false;
			}
			work_filled = true;
			pthread_cond_wait(&gws_cond, stgd_lock);
			ts = __total_staged();
		}
		mutex_unlock(stgd_lock);

		if (ts > max_staged) {
			/* Keeps slowly generating work even if it's not being
			 * used to keep last_getwork incrementing and to see
			 * if pools are still alive. */
			if (work_emptied && max_queue < opt_queue) {
				max_queue++;
				work_emptied = false;
			}
			work_filled = true;
			work = hash_pop(false);
			if (work)
				discard_work(work);
			continue;
		}

		if (work)
			discard_work(work);
		work = make_work();

		if (lagging && !pool_tset(cp, &cp->lagging)) {
			applog(LOG_WARNING, "Pool %d not providing work fast enough", cp->pool_no);
			cp->getfail_occasions++;
			total_go++;
			if (!pool_localgen(cp) && max_queue < opt_queue)
				applog(LOG_INFO, "Increasing queue to %d", ++max_queue);
		}
		pool = select_pool(lagging);
retry:
		if (pool->has_stratum) {
			while (!pool->stratum_active || !pool->stratum_notify) {
				struct pool *altpool = select_pool(true);

				cgsleep_ms(5000);
				if (altpool != pool) {
					pool = altpool;
					goto retry;
				}
			}
			gen_stratum_work(pool, work);
			applog(LOG_DEBUG, "Generated stratum work");
			stage_work(work);
			continue;
		}

		if (opt_benchfile) {
			get_benchfile_work(work);
			applog(LOG_DEBUG, "Generated benchfile work");
			stage_work(work);
			continue;
		} else if (opt_benchmark) {
			get_benchmark_work(work);
			applog(LOG_DEBUG, "Generated benchmark work");
			stage_work(work);
			continue;
		}
	}
	return 0;
}
