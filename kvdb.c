#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define _XOPEN_SOURCE 700
#define MAX_KEY_LENGTH 256
#define MAX_VALUE_LENGTH 256
#define TIMESTAMP_LENGTH 24
#define MAX_CACHE_SIZE 1000

typedef struct {
  char key[MAX_KEY_LENGTH];
  char value[MAX_VALUE_LENGTH];
  char first_set_timestamp[TIMESTAMP_LENGTH];
  char last_set_timestamp[TIMESTAMP_LENGTH];
} KeyValue;

typedef struct {
  KeyValue data[MAX_CACHE_SIZE];
  int capacity;
} Cache;

// Sharable memory, to deal with multi-processes enviroment
Cache *cache;
sem_t *cache_lock;

void initialize_cache() {
  int cache_size = sizeof(Cache);
  int fd = shm_open("/kvdb_cache", O_CREAT | O_RDWR, 0666);
  ftruncate(fd, cache_size);
  cache = mmap(NULL, cache_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);

  cache->capacity = 0;

  cache_lock = sem_open("/kvdb_cache_lock", O_CREAT, 0666, 1);
  if (cache_lock == SEM_FAILED) {
    perror("Failed to create semaphore");
    exit(EXIT_FAILURE);
  }
}

void cleanup_cache() {
  sem_close(cache_lock);
  sem_unlink("/kvdb_cache_lock");

  munmap(cache, sizeof(Cache));
  shm_unlink("/kvdb_cache");
}

// Implement LRU feature to the cache, for better performance in general
void add_to_cache(const KeyValue *kv) {
  sem_wait(cache_lock); // Acquire the lock

  // Search for the key in the cache
  for (int i = 0; i < cache->capacity; i++) {
    if (strcmp(cache->data[i].key, kv->key) == 0) {
      // Key found, update the value and timestamps
      strcpy(cache->data[i].value, kv->value);
      strcpy(cache->data[i].last_set_timestamp, kv->last_set_timestamp);

      // Move the updated entry to the front of the cache (LRU feature)
      memmove(cache->data + 1, cache->data, i * sizeof(KeyValue));
      memcpy(cache->data, &cache->data[i], sizeof(KeyValue));

      sem_post(cache_lock); // Release the lock
      return;
    }
  }

  // If cache is full, remove the least recently used entry
  if (cache->capacity >= MAX_CACHE_SIZE) {
    memmove(cache->data, cache->data + 1,
            (cache->capacity - 1) * sizeof(KeyValue));
  } else {
    cache->capacity++;
  }

  // Insert the new key-value pair at the front of the cache
  memmove(cache->data + 1, cache->data,
          (cache->capacity - 1) * sizeof(KeyValue));
  memcpy(cache->data, kv, sizeof(KeyValue));

  sem_post(cache_lock); // Release the lock
}

KeyValue *get_from_cache(const char *key) {
  sem_wait(cache_lock); // Acquire the lock

  for (int i = 0; i < cache->capacity; i++) {
    if (strcmp(cache->data[i].key, key) == 0) {
      KeyValue *result = &cache->data[i];

      // Move the accessed entry to the front of the cache (LRU feature)
      memmove(cache->data + 1, cache->data, i * sizeof(KeyValue));
      memcpy(cache->data, &cache->data[i], sizeof(KeyValue));

      sem_post(cache_lock); // Release the lock

      return result;
    }
  }

  sem_post(cache_lock); // Release the lock

  return NULL;
}

void delete_from_cache(const char *key) {
  sem_wait(cache_lock); // Acquire the lock

  for (int i = 0; i < cache->capacity; i++) {
    if (strcmp(cache->data[i].key, key) == 0) {
      // Shift the remaining entries to fill the gap
      memmove(cache->data + i, cache->data + i + 1,
              (cache->capacity - i - 1) * sizeof(KeyValue));
      cache->capacity--;

      break;
    }
  }

  sem_post(cache_lock); // Release the lock
}

// A simple method to make sure whether other process is holding a lock
int is_lock_file_present() {
  int lock_file = open("kvdb.lock", O_RDONLY);
  if (lock_file != -1) {
    close(lock_file);
    return 1;
  }
  return 0;
}

void acquire_lock() {
  // Will create `kvdb_lock`, if not exist
  sem_t *lock = sem_open("/kvdb_lock", O_CREAT, 0666, 1);
  if (lock == SEM_FAILED) {
    printf("Error acquiring lock\n");
    return;
  }
  sem_wait(lock);
  sem_close(lock);
}

void release_lock() {
  // Delete `kvdb_lcok` created in acquire_lock()
  sem_t *lock = sem_open("/kvdb_lock", O_CREAT, 0666, 1);
  if (lock == SEM_FAILED) {
    printf("Error releasing lock\n");
    return;
  }
  sem_post(lock);
  sem_close(lock);
}

// Main feature of kv database -- set()
void set_value(const char *key, const char *value) {
  if (is_lock_file_present()) {
    printf("Another process is currently setting a value. Please try again "
           "later.\n");
    return;
  }

  acquire_lock();

  int db_file = open("kvdb.dat", O_RDWR | O_CREAT, 0666);
  if (db_file == -1) {
    printf("Error opening database file\n");
    release_lock();
    return;
  }

  KeyValue kv;
  while (read(db_file, &kv, sizeof(KeyValue)) == sizeof(KeyValue)) {
    if (strcmp(kv.key, key) == 0) {
      printf("key found, and we are going to re-write\n");

      time_t now = time(NULL);
      struct tm *timeinfo = localtime(&now);
      strftime(kv.last_set_timestamp, TIMESTAMP_LENGTH, "%Y-%m-%d %H:%M:%S",
               timeinfo);
      strcpy(kv.value, value);

      lseek(db_file, -sizeof(KeyValue), SEEK_CUR);
      write(db_file, &kv, sizeof(KeyValue));

      add_to_cache(&kv);

      release_lock();
      close(db_file);
      return;
    }
  }

  // Key not found, append a new key-value pair
  lseek(db_file, 0, SEEK_END);
  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);
  strftime(kv.first_set_timestamp, TIMESTAMP_LENGTH, "%Y-%m-%d %H:%M:%S",
           timeinfo);
  strcpy(kv.key, key);
  strcpy(kv.value, value);
  strcpy(kv.last_set_timestamp, kv.first_set_timestamp);
  write(db_file, &kv, sizeof(KeyValue));

  add_to_cache(&kv);

  release_lock();
  close(db_file);
}

// Main feature of kv database -- get()
void get_value(const char *key) {
  if (is_lock_file_present()) {
    printf("Another process is currently setting a value. Please try again "
           "later.\n");
    return;
  }

  KeyValue *cached_kv = get_from_cache(key);
  if (cached_kv != NULL) {
    printf("Value found in cache: %s\n", cached_kv->value);
    return;
  }

  acquire_lock();

  int db_file = open("kvdb.dat", O_RDONLY);
  if (db_file == -1) {
    printf("Error opening database file\n");
    release_lock();
    return;
  }

  KeyValue kv;
  while (read(db_file, &kv, sizeof(KeyValue)) == sizeof(KeyValue)) {
    if (strcmp(kv.key, key) == 0) {
      printf("%s\n", kv.value);
      add_to_cache(&kv);
      release_lock();
      close(db_file);
      return;
    }
  }

  printf("Key not found\n");
  release_lock();
  close(db_file);
}

// Main feature of kv database -- delete()
void delete_value(const char *key) {
  acquire_lock();

  int db_file = open("kvdb.dat", O_RDWR);
  if (db_file == -1) {
    printf("Error opening database file\n");
    release_lock();
    return;
  }

  // Create tmp file can make sure original data file remain intact, untill the
  // deletion process is complete
  int temp_file = open("temp.dat", O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (temp_file == -1) {
    printf("Error creating temporary file\n");
    release_lock();
    close(db_file);
    return;
  }

  KeyValue kv;
  while (read(db_file, &kv, sizeof(KeyValue)) == sizeof(KeyValue)) {
    if (strcmp(kv.key, key) != 0) {
      write(temp_file, &kv, sizeof(KeyValue));
    } else {
      // Remove the key from the cache if it exists
      delete_from_cache(key);
    }
  }

  close(db_file);
  close(temp_file);

  if (remove("kvdb.dat") != 0) {
    printf("Error deleting database file\n");
    release_lock();
    return;
  }

  // After the original file be deleted successfully, we can rename tmp file
  if (rename("temp.dat", "kvdb.dat") != 0) {
    printf("Error renaming temporary file\n");
    release_lock();
    return;
  }

  release_lock();
}

// Main feature of kv database -- ts()
void timestamp_value(const char *key) {
  KeyValue *cached_kv = get_from_cache(key);
  if (cached_kv != NULL) {
    printf("First set (cached): %s\nLast set (cached): %s\n",
           cached_kv->first_set_timestamp, cached_kv->last_set_timestamp);
    return;
  }

  acquire_lock();

  int db_file = open("kvdb.dat", O_RDONLY);
  if (db_file == -1) {
    printf("Error opening database file\n");
    release_lock();
    return;
  }

  KeyValue kv;
  while (read(db_file, &kv, sizeof(KeyValue)) == sizeof(KeyValue)) {
    if (strcmp(kv.key, key) == 0) {
      printf("First set: %s\nLast set: %s\n", kv.first_set_timestamp,
             kv.last_set_timestamp);
      add_to_cache(&kv);
      release_lock();
      close(db_file);
      return;
    }
  }

  printf("Key not found\n");
  release_lock();
  close(db_file);
}

int main(int argc, char *argv[]) {
  initialize_cache();

  if (argc <= 1) {
    printf("No command provided.\n");
    cleanup_cache();
    return 0;
  }

  char *command = argv[1];

  if (strcmp(command, "set") == 0) {
    if (argc != 4) {
      printf("Invalid set command. Usage: ./kvdb set <key> <value>\n");
      cleanup_cache();
      return 0;
    }

    char *key = argv[2];
    char *value = argv[3];
    set_value(key, value);
  } else if (strcmp(command, "get") == 0) {
    if (argc != 3) {
      printf("Invalid get command. Usage: ./kvdb get <key>\n");
      cleanup_cache();
      return 0;
    }

    char *key = argv[2];
    get_value(key);
  } else if (strcmp(command, "del") == 0) {
    if (argc != 3) {
      printf("Invalid del command. Usage: ./kvdb del <key>\n");
      cleanup_cache();
      return 0;
    }

    char *key = argv[2];
    delete_value(key);
  } else if (strcmp(command, "ts") == 0) {
    if (argc != 3) {
      printf("Invalid ts command. Usage: ./kvdb ts <key>\n");
      cleanup_cache();
      return 0;
    }

    char *key = argv[2];
    timestamp_value(key);
  } else {
    printf("Invalid command\n");
  }

  cleanup_cache();

  return 0;
}

// Use this part of code, if you want interactive way to run the program
// int main() {
//   char command[20];

//   initialize_cache();

//   while (1) {
//     printf(">> ");
//     scanf("%s", command);

//     if (strcmp(command, "set") == 0) {
//       char key[MAX_KEY_LENGTH];
//       char value[MAX_VALUE_LENGTH];
//       scanf("%s %s", key, value);
//       set_value(key, value);
//     } else if (strcmp(command, "get") == 0) {
//       char key[MAX_KEY_LENGTH];
//       scanf("%s", key);
//       get_value(key);
//     } else if (strcmp(command, "del") == 0) {
//       char key[MAX_KEY_LENGTH];
//       scanf("%s", key);
//       delete_value(key);
//     } else if (strcmp(command, "ts") == 0) {
//       char key[MAX_KEY_LENGTH];
//       scanf("%s", key);
//       timestamp_value(key);
//     } else if (strcmp(command, "exit") == 0) {
//       break;
//     } else {
//       printf("Invalid command\n");
//     }
//   }

//   cleanup_cache();

//   return 0;
// }