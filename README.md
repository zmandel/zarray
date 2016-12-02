# zarray
c++ multithreaded template library for 32bit windows programs to let them allocate the full 4GB of memory available and overcomes the Windows maximum 2GB.

The template works with any class type and gives you a familiar array-looking object that efficiently and transparently uses memory-mapped files to reach a total of 4GB (among all array instances across all types).

For efficient use, make sure that video or other drivers are not already using too much of your RAM, else that reduces from the total 4GB available.

Also includes a common multithread library used by zarray and by the thread creator and manager library.
