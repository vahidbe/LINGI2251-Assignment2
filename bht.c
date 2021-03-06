#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <assert.h>
#include <sys/time.h>

#define NBUCKET 5          // Number of buckets in hash table
#define NENTRY 1000000     // Number of possible entries per bucket
#define NKEYS 10000       // Number of keys to insert and look up

// An entry in the hash table:
struct entry {
  int key;            // the key
  int value;          // the value
  int inuse;          // is entry in use?
};
// Statically allocate the hash table to avoid using pointers:
struct entry table[NBUCKET][NENTRY];

// An array of keys that we insert and lookup
int keys[NKEYS];

int nthread = 1;
volatile int done;

// The lock that serializes print()'s.
pthread_mutex_t print_lock;

// The lock that serializes get()'s.
pthread_mutex_t get_lock;

// Readers count.
int n_readers = 0;

// The semaphore that serializes put()'s.
sem_t put_sem;

double now() {
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Print content of entries in hash table that are in use.
static void print(void) {
  int b, i;
  for (b = 0; b < NBUCKET; b++) {
    printf("%d: ", b);
    for (i = 0; i < NBUCKET; i++) {
      if (table[b][i].inuse)
	printf("(%d, %d)", table[b][i].key, table[b][i].value);
    }
    printf("\n");
  }
}

// Insert (key, value) pair into hash table.
static void put(int key, int value) {
  assert(sem_wait(&put_sem) == 0);
  int b = key % NBUCKET;
  int i;
  // Loop up through the entries in the bucket to find an unused one:
  for (i = 0; i < NENTRY; i++) {
    if (!table[b][i].inuse) {
      table[b][i].key = key;
      table[b][i].value = value;
      table[b][i].inuse = 1;
      assert(sem_post(&put_sem) == 0);
      return;
    }
  }
  assert(0);
}

// Lookup key in hash table.  The lock serializes the lookups.
static int get(int key) {
  assert(pthread_mutex_lock(&get_lock) == 0);
  n_readers++;
  if (n_readers == 1) {
    sem_wait(&put_sem);
  }
  assert(pthread_mutex_unlock(&get_lock) == 0);

  int b = key % NBUCKET;
  int i;
  int v = -1;
  for (i = 0; i < NENTRY; i++) {
    if (table[b][i].key == key && table[b][i].inuse)  {
      v = table[b][i].value;
      break;
    }
  }

  assert(pthread_mutex_lock(&get_lock) == 0);
  n_readers--;
  if (n_readers == 0) {
    sem_post(&put_sem);
  }
  assert(pthread_mutex_unlock(&get_lock) == 0);
  return v;
}

static void *put_thread(void *xa) {
  long n = (long) xa;
  int i;
  int b = NKEYS/nthread;

  for (i = 0; i < b; i++) {
    put(keys[b*n + i], n);
  }
}

static void *get_thread(void *xa) {
  long n = (long) xa;
  int i;
  int k = 0;
  int b = NKEYS/nthread;

  for (i = 0; i < b; i++) {
    int v = get(keys[b*n + i]);
    if (v == -1) k++;
  }

  assert(pthread_mutex_lock(&print_lock) == 0);
  printf("%ld: %d keys missing\n", n, k);
  assert(pthread_mutex_unlock(&print_lock) == 0);
}

int main(int argc, char *argv[]) {
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);

  // Initialize lock
  assert(pthread_mutex_init(&print_lock, NULL) == 0);
  assert(pthread_mutex_init(&get_lock, NULL) == 0);
  assert(sem_init(&put_sem, 0, 1) == 0);

  // Allocate handles for pthread_join() below.
  tha = malloc(nthread * sizeof(pthread_t));

  // Generate some keys to insert and then lookup again
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (i = 0; i < NKEYS; i++) {
    keys[i] = random();
    assert(keys[i] > 0);
  }

  t0 = now();
  // Create nthread put threads
  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) i) == 0);
  }
  // Wait until they are all done.
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("completion time for put phase = %f\n", t1-t0);

  t0 = now();

  // Create nthread get threads
  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *) i) == 0);
  }
  // Wait until they are all done.
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }

  free(tha);
  t1 = now();

  printf("completion time for get phase = %f\n", t1-t0);
}
