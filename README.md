## Requirement ##
* Due: March 1, 2012
* The MINIX IPCs do not allow a user process (thread) to send a message to another. In this project, we ask you to design and implement a set of system calls that will allow user processes to send and receive messages to one another and add additional functionalities to the native MINIX IPC calls.
* One of your IPC calls will allow a user to create an **interest group**,  the second one will declare the process as a **publisher** to the group. Another will allow the process to declare itself as a **subscriber** to an existing interest group. 
* After declaration, a publisher can use the system call **publish** to send a message to an interest group and a subscriber can use the system call **retrieve** to receive a message from an interest group.
* While a publisher is sending to an interest group, no other user can send or retrieve from the interest group. However, multiple subscribers can be retrieving messages from the same interest group at the same time.
* You will maintain a buffer that can contain **5 messages** for each interest group. No publisher can send to the group when the buffer is full. A message is removed from the buffer after it has been received by all the subscribers.

----------

* We ask you to specifically address these question: 
* * First: Can deadlock occur with your IPCs? If not, why? If yes, do you detect deadlock? If so, how and can you recover from it?. If you do not detect deadlock, you should also say why?
* * Secondly, what other conditions will require the super user to recover from them? Why? And how does the super user become aware of the conditions? 
* Your design document should include the API’s of the system calls, and the API should specify the following:
* * The names and parameters of the system calls
* * The blocking conditions, if any, for the system calls
* * The exception conditions of the system calls

----------

## You should submit the following: ##
* A bootable MINIX with your new IPC system calls
* Test programs that show your new IPC system calls meeting their requirements 
* A readme file that describes how to install your enhanced MINIX and how to execute your test programs
* A six-page (min. 10 point fonts) design document that includes the following: 
* Manual pages for your new system calls
* The design of your system calls
* Possible exceptions and their handling methods
* The reasons behind your decision to make the retrieve system call non-blocking or blocking.
* Detection and recovery from deadlock if needed. Or why if not needed,
* Resource management, if any.
* Each team member must submit a self evaluation