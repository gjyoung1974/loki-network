#include <llarp.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

struct llarp_main {
  struct llarp_alloc mem;
  struct llarp_router *router;
  struct llarp_threadpool *worker;
  struct llarp_threadpool *thread;
  struct llarp_logic *logic;
  struct llarp_config *config;
  struct llarp_nodedb *nodedb;
  struct llarp_ev_loop *mainloop;
  char nodedb_dir[256];
  int exitcode;
};

void iter_main_config(struct llarp_config_iterator *itr, const char *section,
                      const char *key, const char *val) {
  struct llarp_main *m = (struct llarp_main *)itr->user;

  if (!strcmp(section, "router")) {
    if (!strcmp(key, "threads")) {
      int workers = atoi(val);
      if (workers > 0 && m->worker == NULL) {
        m->worker = llarp_init_threadpool(workers);
      }
    }
  }
  if (!strcmp(section, "netdb")) {
    if (!strcmp(key, "dir")) {
      strncpy(m->nodedb_dir, val, sizeof(m->nodedb_dir));
    }
  }
}

static void progress() {
  printf(".");
  fflush(stdout);
}

int shutdown_llarp(struct llarp_main *m) {
  printf("Shutting down ");
  
  progress();
  if(m->mainloop)
    llarp_ev_loop_stop(m->mainloop);

  progress();
  if(m->worker)
    llarp_threadpool_stop(m->worker);
  
  progress();
  
  if(m->worker)
    llarp_threadpool_join(m->worker);
  
  progress();
  if (m->logic)
  {
    llarp_logic_stop(m->logic);
  }

  progress();
  if(m->router)
    llarp_stop_router(m->router);
  
  progress();
  llarp_free_router(&m->router);
  
  progress();
  llarp_free_config(&m->config);
  
  progress();
  llarp_ev_loop_free(&m->mainloop);
  
  progress();
  llarp_free_threadpool(&m->worker);
  
  progress();
  
  llarp_free_logic(&m->logic);
  progress();
  
  printf("\n");
  fflush(stdout);
  return m->exitcode;
}

struct llarp_main llarp = {
  {0,0,0},
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  {0},
  1
};

void run_net(void * user)
{
  llarp_ev_loop_run(user);
}


void handle_signal(int sig)
{
  printf("interrupted\n");
  llarp_ev_loop_stop(llarp.mainloop);
  llarp_logic_stop(llarp.logic);
  printf("closing...");
}

int main(int argc, char *argv[]) {
  const char *conffname = "daemon.ini";
  if (argc > 1) conffname = argv[1];
  llarp_mem_jemalloc(&llarp.mem);
  struct llarp_alloc * mem = &llarp.mem;
  llarp_new_config(&llarp.config);
  llarp_ev_loop_alloc(&llarp.mainloop);
  printf("%s loading config file %s\n", LLARP_VERSION, conffname);
  if (!llarp_load_config(llarp.config, conffname)) {
    struct llarp_config_iterator iter;
    iter.user = &llarp;
    iter.visit = iter_main_config;
    llarp_config_iter(llarp.config, &iter);

    llarp.nodedb = llarp_nodedb_new(mem);
    
    if (llarp.nodedb_dir[0]) {
      llarp.nodedb_dir[sizeof(llarp.nodedb_dir) - 1] = 0;
      char *dir = llarp.nodedb_dir;
      if (llarp_nodedb_ensure_dir(dir)) {
        // ensure worker thread pool
        if (!llarp.worker) llarp.worker = llarp_init_threadpool(2);
        // ensure thread
        llarp.thread = llarp_init_threadpool(1);
        llarp.logic = llarp_init_logic(mem);
        
        llarp.router = llarp_init_router(mem, llarp.worker, llarp.mainloop, llarp.logic);

        if (llarp_configure_router(llarp.router, llarp.config)) {
          signal(SIGINT, handle_signal);
          printf("starting router\n");
          llarp_run_router(llarp.router);
          // run mainloop
          struct llarp_thread_job job = {
            .user = llarp.mainloop,
            .work = &run_net
          };
          llarp_threadpool_queue_job(llarp.thread, job);
          printf("running\n");
          llarp.exitcode = 0;
          llarp_logic_mainloop(llarp.logic);
        } else
          printf("Failed to configure router\n");
      } else
        printf("failed to initialize nodedb at %s\n", dir);
    } else
      printf("no nodedb defined\n");
    
    return shutdown_llarp(&llarp);
  } else
    printf("Failed to load config %s\n", conffname);

  return 1;
}
