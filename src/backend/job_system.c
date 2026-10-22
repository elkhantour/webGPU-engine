#include "job_system.h"
#include <stdlib.h>
#include <unistd.h>

static void q_push(JobQueue *q, Job *j) {
  j->next = NULL;
  if (q->tail)
    q->tail->next = j;
  else
    q->head = j;
  q->tail = j;
}

static Job *q_pop(JobQueue *q) {
  Job *j = q->head;
  if (j) {
    q->head = j->next;
    if (!q->head)
      q->tail = NULL;
  }
  return j;
}

void job_system_destroy(JobSystem *js) {
  atomic_store(&js->running, false);
  pthread_cond_broadcast(&js->pending_cv);
  for (int i = 0; i < js->thread_count; i++)
    pthread_join(js->threads[i], NULL);
  job_system_draw_callback(js);
  free(js->threads);
}

void job_group_init(JobGroup *g, int cap) {
  *g = (JobGroup){0};
  g->cap = cap;
  atomic_init(&g->active, 0);
  atomic_init(&g->remaining, 0);
}

static void enqueue_pending(JobSystem *js, Job *j) {
  pthread_mutex_lock(&js->pending_lock);
  q_push(&js->pending, j);
  pthread_mutex_unlock(&js->pending_lock);
  pthread_cond_signal(&js->pending_cv);
}

static void group_promote(JobSystem *js, JobGroup *g) {
  while (g->backlog.head && atomic_load(&g->active) < g->cap) {
    Job *b = q_pop(&g->backlog);
    atomic_fetch_add(&g->active, 1);
    enqueue_pending(js, b);
  }
}

static void *worker_main(void *arg) {
  JobSystem *js = arg;
  for (;;) {
    pthread_mutex_lock(&js->pending_lock);
    while (!js->pending.head && atomic_load(&js->running))
      pthread_cond_wait(&js->pending_cv, &js->pending_lock);

    Job *job = q_pop(&js->pending);
    pthread_mutex_unlock(&js->pending_lock);

    if (!job)
      break; // shutting down and queue is empty

    job->work(job->data);
    if (job->group)
      atomic_fetch_sub(&job->group->active, 1);

    pthread_mutex_lock(&js->completed_lock);
    q_push(&js->completed, job);
    pthread_mutex_unlock(&js->completed_lock);
  }
  return NULL;
}

void job_system_init(JobSystem *js, int n) {
  *js = (JobSystem){0};
  if (n <= 0) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    n = cores > 2 ? (int)cores - 1 : 1; // leave a core for main thread
  }
  pthread_mutex_init(&js->pending_lock, NULL);
  pthread_cond_init(&js->pending_cv, NULL);
  pthread_mutex_init(&js->completed_lock, NULL);
  atomic_init(&js->running, true);
  atomic_init(&js->in_flight, 0);

  js->thread_count = n;
  js->threads = malloc(sizeof(pthread_t) * n);
  for (int i = 0; i < n; i++)
    pthread_create(&js->threads[i], NULL, worker_main, js);
}

void job_system_submit(JobSystem *js, JobGroup *g, JobWorkFn work,
                       JobDoneFn done, void *data) {
  Job *j = malloc(sizeof(Job));
  *j = (Job){.work = work, .done = done, .data = data, .group = g};
  atomic_fetch_add(&js->in_flight, 1);

  if (g) {
    atomic_fetch_add(&g->remaining, 1);
    // keep FIFO: if anything is already backlogged, queue behind it
    if (g->cap > 0 && (g->backlog.head || atomic_load(&g->active) >= g->cap)) {
      q_push(&g->backlog, j);
      return;
    }
    atomic_fetch_add(&g->active, 1);
  }
  enqueue_pending(js, j);
}

void job_system_draw_callback(void *data) {

  JobSystem *js = (JobSystem *)data;
  // swap the completed list out so workers aren't blocked while we finalize
  pthread_mutex_lock(&js->completed_lock);
  JobQueue local = js->completed;
  js->completed = (JobQueue){0};
  pthread_mutex_unlock(&js->completed_lock);

  Job *j;
  while ((j = q_pop(&local))) {
    JobGroup *g = j->group;
    JobDoneFn done = j->done;
    void *data = j->data;
    free(j);
    atomic_fetch_sub(&js->in_flight, 1);
    if (g) {
      group_promote(js, g); // before done(), which may destroy g
      atomic_fetch_sub(&g->remaining, 1);
    }
    if (done)
      done(data);
  }
}

void job_system_wait_idle(JobSystem *js) {
  while (atomic_load(&js->in_flight) > 0) {
    job_system_draw_callback(js);
    usleep(500);
  }
}
