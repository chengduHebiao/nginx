
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <nginx.h>


static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);
static ngx_int_t ngx_core_module_init(ngx_cycle_t *cycle);


typedef struct {
     ngx_str_t  user;
     int        daemon;
     int        single;
     ngx_str_t  pid;
} ngx_core_conf_t;


static ngx_str_t  core_name = ngx_string("core");

static ngx_command_t  ngx_core_commands[] = {

    { ngx_string("user"),
      NGX_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_core_str_slot,
      0,
      offsetof(ngx_core_conf_t, user),
      NULL },

    { ngx_string("daemon"),
      NGX_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_core_flag_slot,
      0,
      offsetof(ngx_core_conf_t, daemon),
      NULL },

    { ngx_string("single_process"),
      NGX_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_core_flag_slot,
      0,
      offsetof(ngx_core_conf_t, single),
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_core_module = {
    NGX_MODULE,
    &core_name,                            /* module context */
    ngx_core_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    ngx_core_module_init,                  /* init module */
    NULL                                   /* init child */
};


ngx_int_t              ngx_max_module;


/* STUB */
uid_t      user;

u_int ngx_connection_counter;

ngx_int_t  ngx_master;
ngx_int_t  ngx_single;


ngx_int_t  ngx_respawn;
ngx_int_t  ngx_terminate;
ngx_int_t  ngx_quit;
ngx_int_t  ngx_reconfigure;
ngx_int_t  ngx_reopen;
ngx_int_t  ngx_change_binary;


int main(int argc, char *const *argv, char **envp)
{
    struct timeval     tv;
    ngx_fd_t           fd;
    ngx_int_t          i;
    ngx_err_t          err;
    ngx_log_t         *log;
    ngx_cycle_t       *cycle, init_cycle;
    ngx_open_file_t   *file;
    ngx_core_conf_t   *ccf;
#if !(WIN32)
    size_t             len;
    char               pid[/* STUB */ 10];
    ngx_file_t         pidfile;
    struct passwd     *pwd;
#endif

#if __FreeBSD__
    ngx_debug_init();
#endif

    /* TODO */ ngx_max_sockets = -1;

    ngx_time_init();
#if (HAVE_PCRE)
    ngx_regex_init();
#endif

    log = ngx_log_init_errlog();

    /* init_cycle->log is required for signal handlers */

    ngx_memzero(&init_cycle, sizeof(ngx_cycle_t));
    init_cycle.log = log;
    ngx_cycle = &init_cycle;

    if (ngx_os_init(log) == NGX_ERROR) {
        return 1;
    }

    ngx_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        ngx_modules[i]->index = ngx_max_module++;
    }

    if (!(init_cycle.pool = ngx_create_pool(1024, log))) {
        return 1;
    }

    if (ngx_set_inherited_sockets(&init_cycle, envp) == NGX_ERROR) {
        return 1;
    }

    cycle = ngx_init_cycle(&init_cycle);
    if (cycle == NULL) {
        return 1;
    }

    ngx_cycle = cycle;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (ccf->single == 1) {
        ngx_master = 0;
        ngx_single = 1;

    } else {
        ngx_master = 1;
        ngx_single = 0;
    }

#if !(WIN32)

    /* STUB */
    if (ccf->user.len) {
        pwd = getpwnam(ccf->user.data);
        if (pwd == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getpwnam(%s) failed", ccf->user);
            return 1;
        }

        user = pwd->pw_uid;
    }
    /* */

    if (ccf->daemon != 0) {
        if (ngx_daemon(cycle->log) == NGX_ERROR) {
            return 1;
        }
    }

    if (dup2(cycle->log->file->fd, STDERR_FILENO) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "dup2(STDERR) failed");
        return 1;
    }

    if (ccf->pid.len == 0) {
        ccf->pid.len = sizeof(NGINX_PID) - 1;
        ccf->pid.data = NGINX_PID;
    }

    len = ngx_snprintf(pid, /* STUB */ 10, PID_T_FMT, ngx_getpid());
    ngx_memzero(&pidfile, sizeof(ngx_file_t));
    pidfile.name = ccf->pid;

    pidfile.fd = ngx_open_file(pidfile.name.data, NGX_FILE_RDWR,
                               NGX_FILE_CREATE_OR_OPEN);

    if (pidfile.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", pidfile.name.data);
        return 1;
    }

    if (ngx_write_file(&pidfile, pid, len, 0) == NGX_ERROR) {
        return 1;
    }

    if (ngx_close_file(pidfile.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", pidfile.name.data);
    }

#endif

    /* a life cycle */

    for ( ;; ) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "new cycle");

        if (ngx_master) {
            ngx_spawn_process(cycle, ngx_worker_process_cycle, NULL,
                              "worker process", NGX_PROCESS_RESPAWN);

        } else {
            ngx_init_temp_number();

            for (i = 0; ngx_modules[i]; i++) {
                if (ngx_modules[i]->init_process) {
                    if (ngx_modules[i]->init_process(cycle) == NGX_ERROR) {
                        /* fatal */
                        exit(1);
                    }
                }
            }
        }

#if 0
        reconfigure = 0;
        reopen = 0;
#endif

        /* a cycle with the same configuration */

        for ( ;; ) {

            /* an event loop */

            for ( ;; ) {

                err = 0;

                if (ngx_single) {
                    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                                   "worker cycle");

                    ngx_process_events(cycle->log);

                } else {
                    ngx_set_errno(0);
                    ngx_msleep(1000);
                    err = ngx_errno;

                    ngx_gettimeofday(&tv);
                    ngx_time_update(tv.tv_sec);

                    if (err) {
                        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, err,
                                       "sleep() exited");
                    }
                }

                if (ngx_quit || ngx_terminate) {
#if !(WIN32)
                    if (ngx_delete_file(pidfile.name.data) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                                      ngx_delete_file_n " \"%s\" failed",
                                      pidfile.name.data);
                    }
#endif

                    ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "exiting");

                    if (ngx_master) {
                        ngx_signal_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));

                        /* TODO: wait workers */

                        ngx_msleep(1000);

                        ngx_gettimeofday(&tv);
                        ngx_time_update(tv.tv_sec);
                    }

                    ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "exit");
                    exit(0);
                }

                if (err == NGX_EINTR) {
                    ngx_respawn_processes(cycle);
                }

                if (ngx_change_binary) {
                    ngx_change_binary = 0;
                    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                                  "changing binary");
                    ngx_exec_new_binary(cycle, argv);
                    /* TODO: quit workers */
                }

                if (ngx_reconfigure) {
                    ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "reconfiguring");
                    break;
                }

                if (ngx_reopen) {
                    ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                                  "reopening logs");
                    ngx_reopen_files(cycle);
                    ngx_reopen = 0;
                }

            }

            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;
            ngx_reconfigure = 0;
            break;
        }
    }
}

#if 0

static ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle)
{
    ngx_int_t         i, n, failed;
    ngx_str_t         conf_file;
    ngx_log_t        *log;
    ngx_conf_t        conf;
    ngx_pool_t       *pool;
    ngx_cycle_t      *cycle, **old;
    ngx_socket_t      fd;
    ngx_core_conf_t  *ccf;
    ngx_open_file_t  *file;
    ngx_listening_t  *ls, *nls;

    log = old_cycle->log;

    if (!(pool = ngx_create_pool(16 * 1024, log))) {
        return NULL;
    }

    if (!(cycle = ngx_pcalloc(pool, sizeof(ngx_cycle_t)))) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    cycle->pool = pool;

    cycle->old_cycle = old_cycle;


    n = old_cycle->pathes.nelts ? old_cycle->pathes.nelts : 10;
    if (!(cycle->pathes.elts = ngx_pcalloc(pool, n * sizeof(ngx_path_t *)))) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    cycle->pathes.nelts = 0;
    cycle->pathes.size = sizeof(ngx_path_t *);
    cycle->pathes.nalloc = n;
    cycle->pathes.pool = pool;


    n = old_cycle->open_files.nelts ? old_cycle->open_files.nelts : 20;
    cycle->open_files.elts = ngx_pcalloc(pool, n * sizeof(ngx_open_file_t));
    if (cycle->open_files.elts == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    cycle->open_files.nelts = 0;
    cycle->open_files.size = sizeof(ngx_open_file_t);
    cycle->open_files.nalloc = n;
    cycle->open_files.pool = pool;


    if (!(cycle->log = ngx_log_create_errlog(cycle, NULL))) {
        ngx_destroy_pool(pool);
        return NULL;
    }


    n = old_cycle->listening.nelts ? old_cycle->listening.nelts : 10;
    cycle->listening.elts = ngx_pcalloc(pool, n * sizeof(ngx_listening_t));
    if (cycle->listening.elts == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    cycle->listening.nelts = 0;
    cycle->listening.size = sizeof(ngx_listening_t);
    cycle->listening.nalloc = n;
    cycle->listening.pool = pool;


    cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *));
    if (cycle->conf_ctx == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }


    if (!(ccf = ngx_pcalloc(pool, sizeof(ngx_core_conf_t)))) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    /* set by pcalloc()
     *
     * ccf->pid = NULL;
     */
    ccf->daemon = -1;
    ccf->single = -1;
    ((void **)(cycle->conf_ctx))[ngx_core_module.index] = ccf;


    ngx_memzero(&conf, sizeof(ngx_conf_t));
    /* STUB: init array ? */
    conf.args = ngx_create_array(pool, 10, sizeof(ngx_str_t));
    if (conf.args == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    conf.ctx = cycle->conf_ctx;
    conf.cycle = cycle;
    /* STUB */ conf.pool = cycle->pool;
    conf.log = log;
    conf.module_type = NGX_CORE_MODULE;
    conf.cmd_type = NGX_MAIN_CONF;

    conf_file.len = sizeof(NGINX_CONF) - 1;
    conf_file.data = NGINX_CONF;

    if (ngx_conf_parse(&conf, &conf_file) != NGX_CONF_OK) {
        ngx_destroy_pool(pool);
        return NULL;
    }


    failed = 0;

    file = cycle->open_files.elts;
    for (i = 0; i < cycle->open_files.nelts; i++) {
        if (file[i].name.data == NULL) {
            continue;
        }

        file[i].fd = ngx_open_file(file[i].name.data,
                                   NGX_FILE_RDWR,
                                   NGX_FILE_CREATE_OR_OPEN|NGX_FILE_APPEND);

ngx_log_debug(log, "OPEN: %d:%s" _ file[i].fd _ file[i].name.data);

        if (file[i].fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_open_file_n " \"%s\" failed",
                          file[i].name.data);
            failed = 1;
            break;
        }

#if (WIN32)
        if (ngx_file_append_mode(file[i].fd) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_file_append_mode_n " \"%s\" failed",
                          file[i].name.data);
            failed = 1;
            break;
        }
#endif
    }

    if (!failed) {
        if (old_cycle->listening.nelts) {
            ls = old_cycle->listening.elts;
            for (i = 0; i < old_cycle->listening.nelts; i++) {
                ls[i].remain = 0;
            }

            nls = cycle->listening.elts;
            for (n = 0; n < cycle->listening.nelts; n++) {
                for (i = 0; i < old_cycle->listening.nelts; i++) {
                    if (ls[i].ignore) {
                        continue;
                    }

                    ngx_log_error(NGX_LOG_INFO, log, 0,
                                   "%X, %X",
                                   *(int *) ls[i].sockaddr,
                                   *(int *) nls[n].sockaddr);

                    if (ngx_memcmp(nls[n].sockaddr,
                                   ls[i].sockaddr, ls[i].socklen) == 0)
                    {
                        fd = ls[i].fd;
#if (WIN32)
                        /*
                         * Winsock assignes a socket number divisible by 4 so
                         * to find a connection we divide a socket number by 4.
                         */

                        fd /= 4;
#endif
                        if (fd >= (ngx_socket_t) cycle->connection_n) {
                            ngx_log_error(NGX_LOG_EMERG, log, 0,
                                        "%d connections is not enough to hold "
                                        "an open listening socket on %s, "
                                        "required at least %d connections",
                                        cycle->connection_n,
                                        ls[i].addr_text.data, fd);
                            failed = 1;
                            break;
                        }

                        nls[n].fd = ls[i].fd;
                        nls[i].remain = 1;
                        ls[i].remain = 1;
                        break;
                    }
                }

                if (nls[n].fd == -1) {
                    nls[n].new = 1;
                }
            }

        } else {
            ls = cycle->listening.elts;
            for (i = 0; i < cycle->listening.nelts; i++) {
                ls[i].new = 1;
            }
        }

        if (!failed) {
            if (ngx_open_listening_sockets(cycle) == NGX_ERROR) {
                failed = 1;
            }
        }
    }

    if (failed) {

        /* rollback the new cycle configuration */

        file = cycle->open_files.elts;
        for (i = 0; i < cycle->open_files.nelts; i++) {
            if (file[i].fd == NGX_INVALID_FILE) {
                continue;
            }

            if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                              ngx_close_file_n " \"%s\" failed",
                              file[i].name.data);
            }
        }

        ls = cycle->listening.elts;
        for (i = 0; i < cycle->listening.nelts; i++) {
            if (ls[i].new && ls[i].fd == -1) {
                continue;
            }

            if (ngx_close_socket(ls[i].fd) == -1) {
                ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                              ngx_close_socket_n " %s failed",
                              ls[i].addr_text.data);
            }
        }

        ngx_destroy_pool(pool);
        return NULL;
    }

    /* commit the new cycle configuration */

    pool->log = cycle->log;


    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->init_module) {
            if (ngx_modules[i]->init_module(cycle) == NGX_ERROR) {
                /* fatal */
                exit(1);
            }
        }
    }

    /* close and delete stuff that lefts from an old cycle */

    /* close the unneeded listening sockets */

    ls = old_cycle->listening.elts;
    for (i = 0; i < old_cycle->listening.nelts; i++) {
        if (ls[i].remain) {
            continue;
        }

        if (ngx_close_socket(ls[i].fd) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                          ngx_close_socket_n " %s failed",
                          ls[i].addr_text.data);
        }
    }


    /* close the unneeded open files */

    file = old_cycle->open_files.elts;
    for (i = 0; i < old_cycle->open_files.nelts; i++) {
        if (file[i].fd == NGX_INVALID_FILE) {
            continue;
        }

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    if (old_cycle->connections == NULL) {
        /* an old cycle is an init cycle */
        ngx_destroy_pool(old_cycle->pool);
        return cycle;
    }

    if (master) {
        ngx_destroy_pool(old_cycle->pool);
        return cycle;
    }

    if (ngx_temp_pool == NULL) {
        ngx_temp_pool = ngx_create_pool(128, cycle->log);
        if (ngx_temp_pool == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "can not create ngx_temp_pool");
            exit(1);
        }

        n = 10;
        ngx_old_cycles.elts = ngx_pcalloc(ngx_temp_pool,
                                          n * sizeof(ngx_cycle_t *));
        if (ngx_old_cycles.elts == NULL) {
            exit(1);
        }
        ngx_old_cycles.nelts = 0;
        ngx_old_cycles.size = sizeof(ngx_cycle_t *);
        ngx_old_cycles.nalloc = n;
        ngx_old_cycles.pool = ngx_temp_pool;

        ngx_cleaner_event.event_handler = ngx_clean_old_cycles;
        ngx_cleaner_event.log = cycle->log;
        ngx_cleaner_event.data = &dumb;
        dumb.fd = (ngx_socket_t) -1;
    }

    ngx_temp_pool->log = cycle->log;

    old = ngx_push_array(&ngx_old_cycles);
    if (old == NULL) {
        exit(1);
    }
    *old = old_cycle;

    if (!ngx_cleaner_event.timer_set) {
        ngx_add_timer(&ngx_cleaner_event, 30000);
        ngx_cleaner_event.timer_set = 1;
    }

    return cycle;
}

#endif


#if 0

static ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle, char **envp)
{
    char                *p, *v;
    ngx_socket_t         s;
    ngx_listening_t     *ls;
    struct sockaddr_in  *addr_in;

    for ( /* void */ ; *envp; envp++) {
        if (ngx_strncmp(*envp, NGINX_VAR, NGINX_VAR_LEN) != 0) {
            continue;
        }

        ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                      "using inherited sockets from \"%s\"", *envp);

        ngx_init_array(cycle->listening, cycle->pool,
                       10, sizeof(ngx_listening_t), NGX_ERROR);

        for (p = *envp + NGINX_VAR_LEN, v = p; *p; p++) {
            if (*p == ':' || *p == ';') {
                s = ngx_atoi(v, p - v);
                if (s == NGX_ERROR) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                                  "invalid socket number \"%s\" "
                                  "in NGINX enviroment variable, "
                                  "ignoring the rest of the variable", v);
                    break;
                }
                v = p + 1;

                if (!(ls = ngx_push_array(&cycle->listening))) {
                    return NGX_ERROR;
                }

                ls->fd = s;

                /* AF_INET only */

                ls->sockaddr = ngx_palloc(cycle->pool,
                                          sizeof(struct sockaddr_in));
                if (ls->sockaddr == NULL) {
                    return NGX_ERROR;
                }

                ls->socklen = sizeof(struct sockaddr_in);
                if (getsockname(s, ls->sockaddr, &ls->socklen) == -1) {
                    ngx_log_error(NGX_LOG_CRIT, cycle->log, ngx_socket_errno,
                                  "getsockname() of the inherited "
                                  "socket #%d failed", s);
                    ls->ignore = 1;
                    continue;
                }

                addr_in = (struct sockaddr_in *) ls->sockaddr;

                if (addr_in->sin_family != AF_INET) {
                    ngx_log_error(NGX_LOG_CRIT, cycle->log, ngx_socket_errno,
                                  "the inherited socket #%d has "
                                  "unsupported family", s);
                    ls->ignore = 1;
                    continue;
                }
                ls->addr_text_max_len = INET_ADDRSTRLEN;

                ls->addr_text.data = ngx_palloc(cycle->pool,
                                                ls->addr_text_max_len);
                if (ls->addr_text.data == NULL) {
                    return NGX_ERROR;
                }

                addr_in->sin_len = 0;

                ls->family = addr_in->sin_family;
                ls->addr_text.len = ngx_sock_ntop(ls->family, ls->sockaddr,
                                                  ls->addr_text.data,
                                                  ls->addr_text_max_len);
                if (ls->addr_text.len == 0) {
                    return NGX_ERROR;
                }
            }
        }

        break;
    }

    return NGX_OK;
}

#endif


static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_int_t         i;
    ngx_listening_t  *ls;

    if (user) {
        if (setuid(user) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setuid() failed");
            /* fatal */
            exit(1);
        }
    }

    ngx_init_temp_number();

    /*
     * disable deleting previous events for the listening sockets because
     * in the worker processes there are no events at all at this point
     */ 
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        ls[i].remain = 0;
    }

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->init_process) {
            if (ngx_modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(1);
            }
        }
    }

    /* TODO: threads: start ngx_worker_thread_cycle() */

    for ( ;; ) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events(cycle->log);

        if (ngx_terminate) {
            ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "exiting");
            exit(0);
        }

        if (ngx_quit) {
            ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                          "gracefully shutdowning");
            break;
        }

        if (ngx_reopen) {
            ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "reopen logs");
            ngx_reopen_files(cycle);
            ngx_reopen = 0;
        }
    }

    ngx_close_listening_sockets(cycle);

    for ( ;; ) {
        if (ngx_event_timer_rbtree == &ngx_event_timer_sentinel) {
            ngx_log_error(NGX_LOG_INFO, cycle->log, 0, "exiting");
            exit(0);
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events(cycle->log);
    }
}


static void ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv)
{
    char             *env[2], *var, *p;
    ngx_int_t         i;
    ngx_exec_ctx_t    ctx;
    ngx_listening_t  *ls;

    ctx.path = argv[0];
    ctx.name = "new binary process";
    ctx.argv = argv;

    var = ngx_alloc(NGINX_VAR_LEN
                            + cycle->listening.nelts * (NGX_INT32_LEN + 1) + 1,
                    cycle->log);

    p = ngx_cpymem(var, NGINX_VAR, NGINX_VAR_LEN);

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        p += ngx_snprintf(p, NGX_INT32_LEN + 2, "%u;", ls[i].fd);
    }

    env[0] = var;
    env[1] = NULL;
    ctx.envp = (char *const *) &env;

    ngx_exec(cycle, &ctx);

    ngx_free(var);
}


static ngx_int_t ngx_core_module_init(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    /*
     * ngx_core_module has a special init procedure: it is called by
     * ngx_init_cycle() before the configuration file parsing to create
     * ngx_core_module configuration and to set its default parameters
     */

    if (((void **)(cycle->conf_ctx))[ngx_core_module.index] != NULL) {
        return NGX_OK;
    }

    if (!(ccf = ngx_pcalloc(cycle->pool, sizeof(ngx_core_conf_t)))) {
        return NGX_ERROR;
    }
    /* set by pcalloc()
     *
     * ccf->pid = NULL;
     */
    ccf->daemon = -1;
    ccf->single = -1;

    ((void **)(cycle->conf_ctx))[ngx_core_module.index] = ccf;

    return NGX_OK;
}
