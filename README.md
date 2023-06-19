# Key-Value Database support Concurrency and Caching

This is a simple implementation of a key-value database with caching. It provides a command-line interface to store and retrieve key-value pairs while utilizing a cache for efficient access.

## Prerequisites

I test this program, in the system:

- gcc (Ubuntu 11.3.0-1ubuntu1~22.04.1) 11.3.0
- VERSION="22.04.1 LTS (Jammy Jellyfish)"

## Getting Started

- Unit Test Mode: The program will execute pre-defined unit tests, and you can refer to the result in `unit_test_result.txt` and `unit_test.png`. During the unit test, a lock file called `kvdb.lock` is intentionally created to simulate another process working with the database, preventing successful acquisition of the lock by other processes.

```
cd kvdb
make clean; make
./unit_test
```

- Interactive Mode: Comment out the current `main()` function and uncomment the alternative `main()` function. This mode supports an interactive way to run the program, allowing you to input commands and observe the results. You can refer to the result in `interactive_mode_result.png`.

## Limitations of my program

1. If we cares about the storage efficiency a lot. We can implement a B+ tree data structure for storage. But that need too much time to implement.

2. I didn't do imput validation. It will be good to add it.

3. I should avoid magic number as well. There are not lots of magic number in this project, so I just ignore it.

4. It would be good to add more error handling.

5. In terms of maximizing the performance, we can consider to implement read-write lock to support multiple read in the same time. In real world, most scenario is read heavy, so it might be helpful. If we want to support read-write lock, we may need to consider whether we allow user get the outdated value or not.(Because we have cache problem) But consider this is a clone of sqlite, most scenario will be ACID transcations. Hence, we better not send outdated data to user.