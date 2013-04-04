chibios_posix
=============

First feature: 
--------------
A lockless queue für (CAN) messages. The only case 
where a thread has to sleep here is when the queue is empty. Then the reader has nothing to do and may sleep anyway.
